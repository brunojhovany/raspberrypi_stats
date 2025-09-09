#include "ssd1306.h"
#include "stats.h"
#include <chrono>
#include <thread>
#include <cstdio>
#include <csignal>
#include <cmath>
#include <cstdlib>

// Bring font locally for portrait text drawing
static const uint8_t font5x7_portrait[] = {
#include "tiny5x7.inc"
};

static bool running = true;
void onSig(int){ running = false; }

static inline void setPortraitPixel(SSD1306& oled, int xp, int yp, bool on) {
    // Portrait buffer is 32x128; rotate CCW into 128x32 device buffer
    // Mapping: xd = yp, yd = 31 - xp
    int xd = yp;
    int yd = 31 - xp;
    oled.setPixel(xd, yd, on);
}

static void drawHLinePortrait(SSD1306& oled, int x, int y, int w, bool on=true) {
    for (int i = 0; i < w; ++i) setPortraitPixel(oled, x + i, y, on);
}

static void drawTextPortrait(SSD1306& oled, int x, int y, const std::string& text) {
    for (char ch : text) {
        uint8_t c = static_cast<uint8_t>(ch);
        if (c < 32 || c > 127) c = '?';
        const uint8_t* glyph = &font5x7_portrait[(c - 32) * 5];
        for (int col = 0; col < 5; ++col) {
            uint8_t bits = glyph[col];
            for (int row = 0; row < 7; ++row) {
                bool on = bits & (1u << row);
                setPortraitPixel(oled, x + col, y + row, on);
            }
        }
        x += 6; // 5 + 1 spacing
    }
}

static void drawCenteredTextPortrait(SSD1306& oled, int y, const std::string& text) {
    int w = static_cast<int>(text.size()) * 6; // approx
    int x = (32 - w) / 2;
    if (x < 0) x = 0;
    drawTextPortrait(oled, x, y, text);
}

// --- Scaled text (integer scaling of 5x7 font) to fit width 32 ---
static void drawScaledTextPortrait(SSD1306& oled, int x, int y, const std::string& text, int scale) {
    if (scale < 1) scale = 1; if (scale > 6) scale = 6;
    for (char ch : text) {
        uint8_t c = static_cast<uint8_t>(ch);
        if (c < 32 || c > 127) c = '?';
        const uint8_t* glyph = &font5x7_portrait[(c - 32) * 5];
        for (int col = 0; col < 5; ++col) {
            uint8_t bits = glyph[col];
            for (int row = 0; row < 7; ++row) {
                if (bits & (1u << row)) {
                    // scale block
                    for (int dx = 0; dx < scale; ++dx)
                        for (int dy = 0; dy < scale; ++dy)
                            setPortraitPixel(oled, x + col*scale + dx, y + row*scale + dy, true);
                }
            }
        }
        x += 5 * scale + scale; // glyph width + spacing
    }
}

static void drawScaledTextPortraitCentered(SSD1306& oled, int y, const std::string& text) {
    if (text.empty()) return;
    // Base width without final trailing space: n*5 + (n-1)*1 = n*6 -1
    int baseW = static_cast<int>(text.size()) * 6 - 1;
    if (baseW <= 0) baseW = static_cast<int>(text.size()) * 6;
    int chosenScale = 1;
    for (int scale = 6; scale >= 1; --scale) { // try largest down to 1
        if (baseW * scale <= 32) { chosenScale = scale; break; }
    }
    int scaledW = baseW * chosenScale;
    int x = (32 - scaledW) / 2;
    if (x < 0) x = 0;
    drawScaledTextPortrait(oled, x, y, text, chosenScale);
}

// Stretched (non-integer) scaling to occupy full 32px width and desired height (up to 28 rows area)
// Uniform (proportional) scaling: attempt to fill width=32; height scales by same factor.
// If resulting height exceeds available space, scale is reduced uniformly.
static void drawIPProportional(SSD1306& oled, const std::string& text, int topY, int heightAvail) {
    if (text.empty()) return;
    if (heightAvail < 7) heightAvail = 7;
    struct Cache {
        std::string lastText;
        int srcCols = 0;
        std::vector<uint8_t> cols; // glyph columns
    };
    static Cache cache;

    if (cache.lastText != text) {
        cache.lastText = text;
        int n = static_cast<int>(text.size());
        cache.srcCols = n * 5 + (n - 1);
        cache.cols.clear();
        if (cache.srcCols <= 0) return;
        cache.cols.resize(cache.srcCols, 0);
        int writeCol = 0;
        for (int i = 0; i < n; ++i) {
            uint8_t c = static_cast<uint8_t>(text[i]);
            if (c < 32 || c > 127) c = '?';
            const uint8_t* glyph = &font5x7_portrait[(c - 32) * 5];
            for (int gc = 0; gc < 5; ++gc) cache.cols[writeCol++] = glyph[gc];
            if (i != n - 1) cache.cols[writeCol++] = 0x00;
        }
    }
    if (cache.srcCols <= 0) return;

    // Desired scale to fill width
    float scale = 32.0f / cache.srcCols;
    float maxScaleHeight = static_cast<float>(heightAvail) / 7.0f;
    if (scale > maxScaleHeight) scale = maxScaleHeight; // constrain by height

    int outW = static_cast<int>(std::round(cache.srcCols * scale));
    if (outW <= 0) return;
    int outH = static_cast<int>(std::round(7 * scale));
    if (outH < 1) outH = 1;
    if (outW > 32) outW = 32;
    if (outH > heightAvail) outH = heightAvail;

    int startX = (32 - outW) / 2; if (startX < 0) startX = 0;
    int startY = topY + (heightAvail - outH) / 2; if (startY < topY) startY = topY;

    for (int dx = 0; dx < outW; ++dx) {
        float srcXf = (dx + 0.5f) / scale; // back to source col
        int sx = static_cast<int>(srcXf);
        if (sx < 0) sx = 0; if (sx >= cache.srcCols) sx = cache.srcCols - 1;
        uint8_t colBits = cache.cols[sx];
        for (int dy = 0; dy < outH; ++dy) {
            float srcYf = (dy + 0.5f) / scale; // back to source row (0..7)
            int sy = static_cast<int>(srcYf);
            if (sy < 0) sy = 0; if (sy > 6) sy = 6;
            if (colBits & (1u << sy)) setPortraitPixel(oled, startX + dx, startY + dy, true);
        }
    }
}

static void drawDonutPortrait(SSD1306& oled, int cx, int cy, int rOuter, int rInner, int percent) {
    if (percent < 0) percent = 0; if (percent > 100) percent = 100;
    float maxDeg = 360.0f * (percent / 100.0f);
    int rO2 = rOuter * rOuter;
    int rI2 = rInner * rInner;
    for (int y = cy - rOuter; y <= cy + rOuter; ++y) {
        for (int x = cx - rOuter; x <= cx + rOuter; ++x) {
            int dx = x - cx;
            int dy = y - cy;
            int r2 = dx*dx + dy*dy;
            if (r2 > rO2 || r2 < rI2) continue;
            float ang = std::atan2(static_cast<float>(dy), static_cast<float>(dx)) * 180.0f / 3.14159265f;
            if (ang < 0) ang += 360.0f;
            if (ang <= maxDeg) setPortraitPixel(oled, x, y, true);
        }
    }
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
    double uvThreshold = 1.20;
    if (const char* envUv = std::getenv("RPI_STATS_UNDERVOLT_THRESH")) {
        double vv = std::atof(envUv);
        if (vv > 0.5 && vv < 2.0) uvThreshold = vv;
    }
    auto lastLog = std::chrono::steady_clock::now();

    while (running) {
        auto s = collectStats();
        oled.clear();

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
            bool low = haveV && volts < uvThreshold && volts > 0.0;
            if (low || s.throttled) {
                int baseY = 101;
                for (int dx = 0; dx < 11; ++dx) {
                    int height = dx / 2 + 1;
                    for (int dy = 0; dy < height; ++dy) setPortraitPixel(oled, 2 + dx, baseY + dy, true);
                }
                setPortraitPixel(oled, 7, baseY + 2, true);
                setPortraitPixel(oled, 7, baseY + 4, true);
                setPortraitPixel(oled, 7, baseY + 6, true);
            }
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

        // --- Periodic log line for journalctl ---
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - lastLog).count() >= logInterval) {
            printf("stats ip=%s cpu=%d ram=%d disk=%d freq=%s temp=%s volt=%s thr=0x%X\n",
                   s.ip_last_octet.c_str(), s.cpu_percent, s.mem_percent, s.disk_percent,
                   s.cpu_freq.c_str(), s.cpu_temp.c_str(), s.voltage.c_str(), s.throttle_raw);
            fflush(stdout);
            lastLog = now;
        }

        oled.display();
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        ++counter;
    }

    return 0;
}
