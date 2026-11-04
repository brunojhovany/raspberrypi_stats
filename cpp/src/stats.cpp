#include "stats.h"
#include <fstream>
#include <sstream>
#include <string>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <sys/statvfs.h>

static std::string runCmd(const char* cmd) {
    std::array<char, 128> buf{};
    std::string out;
    FILE* pipe = popen(cmd, "r");
    if (!pipe) return out;
    while (fgets(buf.data(), buf.size(), pipe)) out += buf.data();
    pclose(pipe);
    // trim
    while (!out.empty() && (out.back()=='\n' || out.back()=='\r' || out.back()==' ')) out.pop_back();
    return out;
}

static int readCpuPercent() {
    // Simple moving average of /proc/stat over 100ms
    auto read = [](){
        std::ifstream f("/proc/stat");
        std::string cpu; long user=0,nicev=0,sys=0,idle=0,iow=0,irq=0,sirq=0,steal=0,guest=0,nguest=0;
        f >> cpu >> user >> nicev >> sys >> idle >> iow >> irq >> sirq >> steal >> guest >> nguest;
        long idleAll = idle + iow;
        long nonIdle = user + nicev + sys + irq + sirq + steal;
        long total = idleAll + nonIdle;
        return std::pair<long,long>(idleAll, total);
    };
    auto p1 = read();
    usleep(100000);
    auto p2 = read();
    long idleDelta = p2.first - p1.first;
    long totalDelta = p2.second - p1.second;
    if (totalDelta <= 0) return 0;
    int cpu = static_cast<int>( (100.0 * (totalDelta - idleDelta)) / totalDelta + 0.5 );
    if (cpu < 0) cpu = 0; if (cpu > 100) cpu = 100;
    return cpu;
}

static int readMemPercent() {
    std::ifstream f("/proc/meminfo");
    std::string key; long val; std::string unit;
    long memTotal=0, memAvail=0;
    while (f >> key >> val >> unit) {
        if (key == "MemTotal:") memTotal = val;
        else if (key == "MemAvailable:") memAvail = val;
        if (memTotal && memAvail) break;
    }
    if (memTotal == 0) return 0;
    long used = memTotal - memAvail;
    return static_cast<int>(100.0 * used / memTotal + 0.5);
}

static int readDiskPercent(const char* path = "/") {
    struct statvfs s{};
    if (statvfs(path, &s) != 0) return 0;
    unsigned long total = s.f_blocks * s.f_frsize;
    unsigned long avail = s.f_bavail * s.f_frsize;
    if (total == 0) return 0;
    unsigned long used = total - avail;
    return static_cast<int>(100.0 * used / total + 0.5);
}

static std::string readCpuFreq() {
    std::ifstream f("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq");
    long khz = 0; f >> khz;
    if (!f) return "N/A";
    double ghz = khz / 1e6;
    char buf[16];
    snprintf(buf, sizeof(buf), "%.1fGHz", ghz);
    return buf;
}

static std::string readCpuTemp() {
    // Prefer vcgencmd; fallback to /sys
    std::string out = runCmd("vcgencmd measure_temp | cut -d '=' -f2 | tr -d '\n' ");
    if (!out.empty()) return out;
    std::ifstream f("/sys/class/thermal/thermal_zone0/temp");
    long milli = 0; f >> milli;
    if (!f) return "N/A";
    char buf[16];
    snprintf(buf, sizeof(buf), "%ld°C", milli/1000);
    return buf;
}

static std::string readIpLastOctet() {
    std::string ip = runCmd("ip -4 addr show scope global | grep -oP 'inet \\K([0-9.]+)' | head -n1");
    if (ip.empty()) return "0";
    auto pos = ip.find_last_of('.');
    if (pos == std::string::npos) return ip;
    return ip.substr(pos+1);
}

static std::string readVoltage() {
    std::string v = runCmd("vcgencmd measure_volts core | cut -d '=' -f2");
    if (v.empty()) return "N/A";
    return v; // typically like 1.2500V
}

// Returns false if vcgencmd failed or its output did not parse.
static bool readThrottleRaw(uint32_t& val) {
    val = 0;
    // RPI_STATS_FAKE_THROTTLED=0x50005 simulates a reading (to test the alert screen)
    const char* fake = std::getenv("RPI_STATS_FAKE_THROTTLED");
    std::string t = fake ? std::string(fake)
                         : runCmd("vcgencmd get_throttled 2>/dev/null | cut -d '=' -f2");
    if (t.rfind("0x", 0) != 0) return false; // expect 0xNNNNN
    char* end = nullptr;
    unsigned long v = std::strtoul(t.c_str(), &end, 16);
    if (end == t.c_str() + 2) return false;   // no hex digits after 0x
    val = static_cast<uint32_t>(v);
    return true;
}

// Fallback when vcgencmd is unavailable: the rpi_volt hwmon driver exposes the
// same firmware under-voltage flag as in0_lcrit_alarm.
static bool readHwmonUndervolt(bool& alarm) {
    for (int i = 0; i < 16; ++i) {
        std::string base = "/sys/class/hwmon/hwmon" + std::to_string(i) + "/";
        std::ifstream n(base + "name");
        std::string name;
        if (!(n >> name) || name != "rpi_volt") continue;
        std::ifstream a(base + "in0_lcrit_alarm");
        int v = 0;
        if (!(a >> v)) return false;
        alarm = v != 0;
        return true;
    }
    return false;
}

Stats collectStats() {
    Stats s;
    s.cpu_percent = readCpuPercent();
    s.mem_percent = readMemPercent();
    s.disk_percent = readDiskPercent("/");
    s.cpu_freq = readCpuFreq();
    s.cpu_temp = readCpuTemp();
    s.ip_last_octet = readIpLastOctet();
    s.voltage = readVoltage();
    s.throttle_ok = readThrottleRaw(s.throttle_raw);
    if (!s.throttle_ok) {
        bool alarm = false;
        if (readHwmonUndervolt(alarm) && alarm) s.throttle_raw = 0x10001; // uv now + since boot
    }
    // Bits (Raspberry Pi docs): 0-3 are "now", 16-19 are "has occurred since boot".
    const uint32_t r = s.throttle_raw;
    s.uv_now    = r & (1u << 0);
    s.cap_now   = r & (1u << 1);
    s.thr_now   = r & (1u << 2);
    s.temp_now  = r & (1u << 3);
    s.uv_boot   = r & (1u << 16);
    s.cap_boot  = r & (1u << 17);
    s.thr_boot  = r & (1u << 18);
    s.temp_boot = r & (1u << 19);
    s.throttled = s.uv_now || s.cap_now || s.thr_now || s.temp_now;
    return s;
}
