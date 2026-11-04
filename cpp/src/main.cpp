#include "ssd1306.h"
#include "stats.h"
#include <chrono>
#include <thread>
#include <cstdio>
#include <csignal>
#include <cmath>
#include <cstdlib>
#include <string>
#include <syslog.h>

#include "portrait.h"
#include "eva_view.h"
#include "saver.h"
#include <ctime>

static bool running = true;
void onSig(int){ running = false; }

static std::string describeThrottle(const Stats& s) {
    std::string d;
    auto add = [&](bool on, const char* name) { if (on) { if (!d.empty()) d += ","; d += name; } };
    add(s.uv_now, "undervoltage");
    add(s.cap_now, "freq-capped");
    add(s.thr_now, "throttled");
    add(s.temp_now, "soft-temp-limit");
    add(s.uv_boot, "undervoltage-since-boot");
    add(s.cap_boot, "freq-capped-since-boot");
    add(s.thr_boot, "throttled-since-boot");
    add(s.temp_boot, "soft-temp-limit-since-boot");
    return d.empty() ? "ok" : d;
}

// Alert lines go to stdout, stderr and syslog/journald (journalctl -t raspberrypi_stats)
static void logEvent(int prio, const char* fmt, const std::string& a, uint32_t raw) {
    char line[256];
    std::snprintf(line, sizeof(line), fmt, a.c_str(), raw);
    std::printf("%s\n", line); std::fflush(stdout);
    std::fprintf(stderr, "%s\n", line); std::fflush(stderr);
    syslog(prio, "%s", line);
}

// Full-screen under-voltage alert (portrait 32x128)
static void drawUndervoltScreen(SSD1306& oled, const Stats& s, long activeSecs, int events) {
    drawBoltPortrait(oled, 6, 3, 2);                       // y 3..26
    drawCenteredTextPortrait(oled, 33, "LOW");
    drawCenteredTextPortrait(oled, 43, "VOLT");
    drawHLinePortrait(oled, 0, 54, 32);
    drawCenteredTextPortrait(oled, 60, fmtDuration(activeSecs));
    char ev[16]; std::snprintf(ev, sizeof(ev), "N:%d", events);
    drawCenteredTextPortrait(oled, 72, ev);
    char raw[12]; std::snprintf(raw, sizeof(raw), "%X", s.throttle_raw);
    drawCenteredTextPortrait(oled, 88, raw);
    drawHLinePortrait(oled, 0, 104, 32);
    drawIPProportional(oled, s.ip_last_octet, 108, 18);
}

// Original layout: IP, freq, CPU donut, alternating RAM/DISK and TEMP/VOLT
static void drawClassicScreen(SSD1306& oled, const Stats& s, int counter) {
    // 1) IP (proportional scaling) in reserved top area
    int ipAreaHeight = 26; // reserved vertical space
    drawIPProportional(oled, s.ip_last_octet, 0, ipAreaHeight);

    // 2) Divider
    drawHLinePortrait(oled, 0, ipAreaHeight, 32);

    // 3) CPU freq (F:xx.xG)
    double freqVal = 0.0;
    try {
        if (!s.cpu_freq.empty()) {
            size_t posG = s.cpu_freq.find('G');
            std::string num = s.cpu_freq.substr(0, posG);
            freqVal = std::stod(num);
        }
    } catch (...) { freqVal = 0.0; }
    char freqBuf[12];
    std::snprintf(freqBuf, sizeof(freqBuf), "%.1fG", freqVal);
    drawCenteredTextPortrait(oled, ipAreaHeight + 4, freqBuf);

    // 4) CPU donut + percent
    int cx = 16, cy = 64;
    drawDonutPortrait(oled, cx, cy, 15, 12, s.cpu_percent);
    char pct[8];
    std::snprintf(pct, sizeof(pct), "%d%%", s.cpu_percent);
    drawCenteredTextPortrait(oled, 58, pct);

    // 5) Lower section (alternate sets)
    bool phaseA = ((counter / 6) % 2 == 0);

    double volts = 0.0; bool haveV = false;
    try { if (!s.voltage.empty() && s.voltage[0] != 'N') { volts = std::stod(s.voltage); haveV = true; } } catch (...) { volts = 0.0; }
    char voltBuf[10];
    if (haveV) std::snprintf(voltBuf, sizeof(voltBuf), "V:%.1f", volts);
    else std::snprintf(voltBuf, sizeof(voltBuf), "V:NA");

    if (phaseA) {
        char ramLine[12]; std::snprintf(ramLine, sizeof(ramLine), "R:%d%%", s.mem_percent);
        drawCenteredTextPortrait(oled, 86, ramLine);
        char diskLine[12]; std::snprintf(diskLine, sizeof(diskLine), "D:%d%%", s.disk_percent);
        drawCenteredTextPortrait(oled, 101, diskLine);
    } else {
        double tempVal = 0.0; bool haveT = false;
        try {
            if (!s.cpu_temp.empty() && s.cpu_temp[0] != 'N') {
                std::string num; num.reserve(6);
                for (char c : s.cpu_temp) {
                    if ((c >= '0' && c <= '9') || c == '.') num.push_back(c); else break;
                }
                if (!num.empty()) { tempVal = std::stod(num); haveT = true; }
            }
        } catch (...) { haveT = false; }
        char tBuf[12];
        if (haveT) std::snprintf(tBuf, sizeof(tBuf), "T:%.1fC", tempVal);
        else std::snprintf(tBuf, sizeof(tBuf), "T:NA");
        drawCenteredTextPortrait(oled, 86, tBuf);

        if (s.throttled) {
            char thr[16]; std::snprintf(thr, sizeof(thr), "H:%X", s.throttle_raw);
            drawCenteredTextPortrait(oled, 101, thr);
        } else {
            drawCenteredTextPortrait(oled, 101, voltBuf);
        }
    }

    // 6) Sticky marker: something happened since boot (cleared only by reboot)
    if (s.uv_boot) drawCenteredTextPortrait(oled, 116, "!UV");
    else if (s.cap_boot || s.thr_boot || s.temp_boot) drawCenteredTextPortrait(oled, 116, "!TH");
}

static long secsBetween(std::chrono::steady_clock::time_point a, std::chrono::steady_clock::time_point b) {
    return static_cast<long>(std::chrono::duration_cast<std::chrono::seconds>(b - a).count());
}

static int envInt(const char* name, int def, int lo, int hi) {
    const char* e = std::getenv(name);
    if (!e || !*e) return def;
    char* end = nullptr;
    long v = std::strtol(e, &end, 0); // accepts 32 or 0x20
    if (end == e || v < lo || v > hi) return def;
    return static_cast<int>(v);
}

// "HH:MM-HH:MM" -> minutes of day; returns false if empty/"off"/invalid
static bool parseWindow(const char* spec, int& start, int& end) {
    if (!spec) return false;
    int h1, m1, h2, m2;
    if (std::sscanf(spec, "%d:%d-%d:%d", &h1, &m1, &h2, &m2) != 4) return false;
    if (h1 < 0 || h1 > 23 || h2 < 0 || h2 > 23 || m1 < 0 || m1 > 59 || m2 < 0 || m2 > 59) return false;
    start = h1 * 60 + m1; end = h2 * 60 + m2;
    return start != end;
}

static bool inWindow(int start, int end) {
    std::time_t t = std::time(nullptr);
    std::tm lt{};
    localtime_r(&t, &lt);
    int now = lt.tm_hour * 60 + lt.tm_min;
    return start < end ? (now >= start && now < end)
                       : (now >= start || now < end); // crosses midnight
}

static void logInfo(const std::string& msg) {
    std::printf("%s\n", msg.c_str()); std::fflush(stdout);
    syslog(LOG_INFO, "%s", msg.c_str());
}

int main() {
    std::signal(SIGINT, onSig);
    std::signal(SIGTERM, onSig);

    SSD1306 oled("/dev/i2c-1", 0x3C);
    if (!oled.init()) return 1;

    int counter = 0;
    // --- Logging & thresholds from environment ---
    int logInterval = 30; // seconds
    if (const char* envLi = std::getenv("RPI_STATS_LOG_INTERVAL")) {
        int v = std::atoi(envLi);
        if (v > 0 && v < 86400) logInterval = v;
    }
    // Keep the alert screen up this many seconds after the under-voltage flag
    // clears, so short dips are still visible.
    int uvHold = 10;
    if (const char* envH = std::getenv("RPI_STATS_UV_HOLD")) {
        int v = std::atoi(envH);
        if (v >= 0 && v < 3600) uvHold = v;
    }
    bool uvBlink = true;
    if (const char* envB = std::getenv("RPI_STATS_UV_BLINK")) uvBlink = std::atoi(envB) != 0;

    // RPI_STATS_VIEW=classic (default) | eva | cycle (alternate every RPI_STATS_CYCLE_SECS)
    enum class View { Classic, Eva, Cycle };
    View view = View::Classic;
    if (const char* envV = std::getenv("RPI_STATS_VIEW")) {
        std::string v = envV;
        if (v == "eva") view = View::Eva;
        else if (v == "cycle") view = View::Cycle;
    }
    int cycleSecs = 10;
    if (const char* envC = std::getenv("RPI_STATS_CYCLE_SECS")) {
        int v = std::atoi(envC);
        if (v > 0 && v < 3600) cycleSecs = v;
    }

    // --- Burn-in protection ---
    // Normal brightness; alerts always go to 0xFF
    const int contrast = envInt("RPI_STATS_CONTRAST", 0x20, 1, 255);
    // Panel off at night unless something is wrong. "off" disables.
    int sleepStart = 0, sleepEnd = 0;
    const char* sleepSpec = std::getenv("RPI_STATS_SLEEP");
    bool sleepOn = parseWindow(sleepSpec ? sleepSpec : "23:00-07:00", sleepStart, sleepEnd);
    // Screensaver: after SAVER_AFTER s of metrics, show the banner and peek at
    // the metrics for SAVER_PEEK s of every SAVER_PERIOD s. SAVER_AFTER=0 disables.
    const int saverAfter = envInt("RPI_STATS_SAVER_AFTER", 300, 0, 86400);
    const int saverPeriod = envInt("RPI_STATS_SAVER_PERIOD", 60, 10, 3600);
    const int saverPeek = envInt("RPI_STATS_SAVER_PEEK", 10, 0, saverPeriod - 1);
    const int saverFps = envInt("RPI_STATS_SAVER_FPS", 8, 1, 20);
    // Shift static layouts 0..2 px vertically every 2 minutes. 0 disables.
    const bool shiftOn = envInt("RPI_STATS_SHIFT", 1, 0, 1) != 0;

    openlog("raspberrypi_stats", LOG_PID, LOG_DAEMON);
    {
        char b[200];
        std::snprintf(b, sizeof(b), "INFO burn-in: contrast=0x%02X sleep=%s saver_after=%ds period=%ds peek=%ds shift=%d",
                      contrast, sleepOn ? (sleepSpec ? sleepSpec : "23:00-07:00") : "off",
                      saverAfter, saverPeriod, saverPeek, shiftOn ? 1 : 0);
        logInfo(b);
    }
    int curContrast = -1;
    bool sleeping = false;
    long saverFrame = 0;
    const auto startTime = std::chrono::steady_clock::now();
    auto metricsSince = startTime; // saver countdown restarts on wake / state change
    const uint32_t kWatched = 0x000F000F; // "now" + "since boot" bits
    uint32_t prevFlags = 0;
    bool firstSample = true;
    bool inverted = false;
    int uvEvents = 0;
    auto uvStart = std::chrono::steady_clock::now();
    auto uvLastSeen = uvStart - std::chrono::hours(1);
    auto lastLog = std::chrono::steady_clock::now();

    while (running) {
        auto s = collectStats();
        auto tick = std::chrono::steady_clock::now();

        // --- Throttle / under-voltage state tracking ---
        uint32_t flags = s.throttle_raw & kWatched;
        if (firstSample) {
            if (!s.throttle_ok)
                logEvent(LOG_WARNING, "WARN vcgencmd get_throttled unavailable, using hwmon fallback%s thr=0x%X", "", s.throttle_raw);
            if (s.uv_now)
                logEvent(LOG_CRIT, "ALERT undervoltage detected at start: %s thr=0x%X", describeThrottle(s), s.throttle_raw);
            else if (flags)
                logEvent(LOG_WARNING, "WARN throttle state at start: %s thr=0x%X", describeThrottle(s), s.throttle_raw);
        } else if (flags != prevFlags) {
            metricsSince = tick; // show the metrics again when something changes
            bool uvRise = s.uv_now && !(prevFlags & 1u);
            bool uvFall = !s.uv_now && (prevFlags & 1u);
            if (uvRise) logEvent(LOG_CRIT, "ALERT undervoltage detected: %s thr=0x%X", describeThrottle(s), s.throttle_raw);
            else if (uvFall) logEvent(LOG_NOTICE, "INFO undervoltage cleared: %s thr=0x%X", describeThrottle(s), s.throttle_raw);
            else logEvent(LOG_WARNING, "WARN throttle state changed: %s thr=0x%X", describeThrottle(s), s.throttle_raw);
        }
        if (s.uv_now) {
            if (std::chrono::duration_cast<std::chrono::seconds>(tick - uvLastSeen).count() > uvHold) {
                uvStart = tick; // new episode
                ++uvEvents;
            }
            uvLastSeen = tick;
        }
        prevFlags = flags;
        firstSample = false;

        bool uvAlert = secsBetween(uvLastSeen, tick) <= uvHold;
        bool alertish = uvAlert || s.throttled;

        // --- Night sleep (alerts wake the panel) ---
        bool asleep = sleepOn && !alertish && inWindow(sleepStart, sleepEnd);
        if (asleep != sleeping) {
            oled.setPower(!asleep);
            sleeping = asleep;
            if (!asleep) metricsSince = tick;
            logInfo(asleep ? "INFO display sleeping (night window)" : "INFO display awake");
        }

        // --- Brightness ---
        int wantContrast = alertish ? 0xFF : contrast;
        if (wantContrast != curContrast) { oled.setContrast(static_cast<uint8_t>(wantContrast)); curContrast = wantContrast; }

        bool wantInvert = !asleep && uvAlert && uvBlink && (counter % 2 == 1);
        if (wantInvert != inverted) { oled.setInverted(wantInvert); inverted = wantInvert; }

        // --- Screensaver window ---
        bool saver = false;
        if (saverAfter > 0 && !alertish && !asleep) {
            long t = secsBetween(metricsSince, tick) - saverAfter;
            if (t >= 0) saver = (t % saverPeriod) < (saverPeriod - saverPeek);
        }

        // --- Pixel shift for the static layouts ---
        static const int kShift[4] = {0, 1, 2, 1};
        setPortraitShift(shiftOn && !saver ? kShift[(secsBetween(startTime, tick) / 120) % 4] : 0);

        // --- Periodic log line ---
        if (secsBetween(lastLog, tick) >= logInterval) {
            printf("stats ip=%s cpu=%d ram=%d disk=%d freq=%s temp=%s volt=%s thr=0x%X mode=%s\n",
                   s.ip_last_octet.c_str(), s.cpu_percent, s.mem_percent, s.disk_percent,
                   s.cpu_freq.c_str(), s.cpu_temp.c_str(), s.voltage.c_str(), s.throttle_raw,
                   asleep ? "sleep" : saver ? "saver" : uvAlert ? "alert" : "metrics");
            fflush(stdout);
            lastLog = tick;
        }

        // --- Render ---
        if (asleep) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        } else if (saver) {
            bool sticky = s.uv_boot || s.cap_boot || s.thr_boot || s.temp_boot;
            for (int f = 0; f < saverFps && running; ++f) {
                oled.clear();
                drawSaverFrame(oled, saverFrame++, sticky);
                oled.display();
                std::this_thread::sleep_for(std::chrono::milliseconds(1000 / saverFps));
            }
        } else {
            oled.clear();
            bool useEva = view == View::Eva ||
                          (view == View::Cycle && (counter / cycleSecs) % 2 == 1);
            if (uvAlert) {
                long secs = secsBetween(uvStart, uvLastSeen);
                if (useEva) drawEvaAlertScreen(oled, s, secs, uvEvents);
                else drawUndervoltScreen(oled, s, secs, uvEvents);
            } else if (useEva) {
                drawEvaScreen(oled, s, counter);
            } else {
                drawClassicScreen(oled, s, counter);
            }
            oled.display();
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
        ++counter;
    }

    // Leave the panel blank on exit: a frozen frame is the worst case for burn-in
    oled.setInverted(false);
    oled.clear();
    oled.display();
    closelog();
    return 0;
}
