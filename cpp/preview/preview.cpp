// Off-device preview: draws each screen into the framebuffer and prints it as
// a 32x128 portrait grid of 0/1 (one "SCREEN <name>" block per view).
#include "ssd1306.h"
#include "stats.h"
#include "eva_view.h"
#include "saver.h"
#include "portrait.h"
#include <cstdio>
#include <string>

static void dump(const SSD1306& o, const char* name) {
    std::printf("SCREEN %s\n", name);
    const auto& b = o.buffer();
    for (int yp = 0; yp < 128; ++yp) {
        for (int xp = 0; xp < 32; ++xp) {
            int xd = yp, yd = 31 - xp;
            std::putchar((b[(yd / 8) * 128 + xd] >> (yd % 8)) & 1 ? '1' : '0');
        }
        std::putchar('\n');
    }
}

int main() {
    SSD1306 o("/nonexistent", 0x3C);
    Stats s;
    s.ip_last_octet = "254"; s.cpu_percent = 24; s.mem_percent = 58; s.disk_percent = 72;
    s.cpu_freq = "1.8GHz"; s.cpu_temp = "51.0'C"; s.voltage = "0.9160V";

    o.clear(); drawEvaScreen(o, s, 0); dump(o, "eva normal");
    Stats b = s; b.throttle_raw = 0x50000; b.uv_boot = true;
    o.clear(); drawEvaScreen(o, b, 0); dump(o, "eva !UV since boot");
    Stats t = s; t.throttle_raw = 0x80008; t.temp_now = t.throttled = true; t.temp_boot = true; t.cpu_temp = "81.2'C";
    o.clear(); drawEvaScreen(o, t, 0); dump(o, "eva temp limit");
    Stats a = s; a.throttle_raw = 0x50005; a.uv_now = a.thr_now = a.throttled = true; a.uv_boot = a.thr_boot = true;
    a.cpu_percent = 27; a.mem_percent = 60; a.cpu_temp = "52.0'C";
    o.clear(); drawEvaAlertScreen(o, a, 42, 3); dump(o, "eva alert");
    const long travel = 128 + 102 + 10;
    long frames[] = {20, 70, 120, travel + 60};
    const char* names[] = {"saver f20", "saver f70", "saver f120", "saver mirrored"};
    for (int i = 0; i < 4; ++i) { o.clear(); drawSaverFrame(o, frames[i], false); dump(o, names[i]); }
    o.clear(); drawSaverFrame(o, 70, true); dump(o, "saver sticky");
    setPortraitShift(2); o.clear(); drawEvaScreen(o, s, 0); dump(o, "eva shift+2"); setPortraitShift(0);
    return 0;
}
