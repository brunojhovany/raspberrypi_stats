#pragma once
#include <string>
#include <cstdint>

struct Stats {
    std::string ip_last_octet; // like Python code shows just last octet
    int cpu_percent = 0;       // 0..100
    int mem_percent = 0;       // 0..100
    int disk_percent = 0;      // 0..100
    std::string cpu_freq;      // e.g. "1.5GHz"
    std::string cpu_temp;      // e.g. "54°C"
    std::string voltage;       // e.g. "1.25V"
    uint32_t throttle_raw = 0; // raw hex flags from vcgencmd get_throttled
    bool throttled = false;    // any throttling/undervoltage active
};

// Collect stats from /proc and vcgencmd; all functions are non-throwing
Stats collectStats();
