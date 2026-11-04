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
    bool throttled = false;    // any throttling/undervoltage active (bits 0-3)
    bool throttle_ok = false;  // vcgencmd answered with a parsable value

    // Decoded flags from vcgencmd get_throttled
    bool uv_now = false;       // bit 0:  under-voltage detected right now
    bool cap_now = false;      // bit 1:  ARM frequency capped
    bool thr_now = false;      // bit 2:  currently throttled
    bool temp_now = false;     // bit 3:  soft temperature limit active
    bool uv_boot = false;      // bit 16: under-voltage has occurred since boot
    bool cap_boot = false;     // bit 17
    bool thr_boot = false;     // bit 18
    bool temp_boot = false;    // bit 19
};

// Collect stats from /proc and vcgencmd; all functions are non-throwing
Stats collectStats();
