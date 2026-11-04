// Evangelion-style alternate view: kanji header, hazard stripes, tag boxes and
// segmented bars, all in the 32x128 portrait canvas.
#include "eva_view.h"
#include "portrait.h"
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>


static void fillRect(SSD1306& o, int x, int y, int w, int h, bool on = true) {
    for (int dy = 0; dy < h; ++dy)
        for (int dx = 0; dx < w; ++dx) setPortraitPixel(o, x + dx, y + dy, on);
}

// Only touches the "ink" pixels, so it works on top of filled boxes too
static void drawTextColor(SSD1306& o, int x, int y, const std::string& t, bool on) {
    for (char ch : t) {
        const uint8_t* g = glyph5x7(ch);
        for (int col = 0; col < 5; ++col)
            for (int row = 0; row < 7; ++row)
                if (g[col] & (1u << row)) setPortraitPixel(o, x + col, y + row, on);
        x += 6;
    }
}

static int textWidth(const std::string& t) { return t.empty() ? 0 : int(t.size()) * 6 - 1; }


// Diagonal warning stripes; phase shifts them so they can "march"
static void hazardStripes(SSD1306& o, int y, int h, int phase = 0) {
    for (int dy = 0; dy < h; ++dy)
        for (int x = 0; x < PW; ++x)
            setPortraitPixel(o, x, y + dy, ((x + dy + phase) % 6) < 3);
}

// Inverted label box: [C] style tag, text cut out of a filled rectangle
static void tagBox(SSD1306& o, int x, int y, const std::string& label) {
    int w = textWidth(label) + 4;
    fillRect(o, x, y, w, 9);
    // chamfer the top-left corner, Eva HUD style
    setPortraitPixel(o, x, y, false);
    drawTextColor(o, x + 2, y + 1, label, false);
}

// Segmented bar, 8 segments of 3 px + 1 px gap = 32 px
static void segBar(SSD1306& o, int y, int pct) {
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    int filled = (pct * 8 + 50) / 100;
    if (pct > 0 && filled == 0) filled = 1;
    for (int i = 0; i < 8; ++i) {
        int x = i * 4;
        if (i < filled) fillRect(o, x, y, 3, 3);
        else fillRect(o, x, y + 2, 3, 1); // empty segment: baseline only
    }
}

static void valueRight(SSD1306& o, int y, const std::string& v) {
    drawTextColor(o, PW - textWidth(v), y, v, true);
}

static int tempValue(const Stats& s, bool& ok) {
    std::string num;
    for (char c : s.cpu_temp) {
        if ((c >= '0' && c <= '9') || c == '.') num.push_back(c); else break;
    }
    ok = !num.empty();
    return ok ? int(std::atof(num.c_str()) + 0.5) : 0;
}

static std::string hostTag() {
    char h[64] = {0};
    if (gethostname(h, sizeof(h) - 1) != 0 || !h[0]) return "RPI";
    std::string t;
    for (int i = 0; h[i] && t.size() < 5; ++i) t.push_back(char(std::toupper(static_cast<unsigned char>(h[i]))));
    return t;
}

// One metric: tag + right-aligned value (+ optional bar under it)
static void metric(SSD1306& o, int y, const char* tag, const std::string& val, int barPct, bool bar) {
    tagBox(o, 0, y, tag);
    valueRight(o, y + 1, val);
    if (bar) segBar(o, y + 11, barPct);
}

static void metrics(SSD1306& o, const Stats& s, int y0, int step, bool bars) {
    char b[12];
    std::snprintf(b, sizeof(b), "%d%%", s.cpu_percent);
    metric(o, y0, "C", b, s.cpu_percent, bars);
    std::snprintf(b, sizeof(b), "%d%%", s.mem_percent);
    metric(o, y0 + step, "R", b, s.mem_percent, bars);
    bool tok = false; int t = tempValue(s, tok);
    if (tok) std::snprintf(b, sizeof(b), "%dC", t); else std::snprintf(b, sizeof(b), "NA");
    metric(o, y0 + 2 * step, "T", b, t * 100 / 85, bars); // bar full at 85C (throttle point)
    std::snprintf(b, sizeof(b), "%d%%", s.disk_percent);
    metric(o, y0 + 3 * step, "D", b, s.disk_percent, bars);
}

static void ipRow(SSD1306& o, int y, const Stats& s) {
    tagBox(o, 0, y, "IP");
    valueRight(o, y + 1, s.ip_last_octet);
}

void drawEvaScreen(SSD1306& o, const Stats& s, int /*counter*/) {
    // Header: 正常 (normal)
    drawKanji(o, 0, 0, K_SEI, true);
    drawKanji(o, 16, 0, K_JOU, true);
    drawHLinePortrait(o, 0, 17, PW);
    hazardStripes(o, 19, 5);

    // CPU / RAM / TEMP / DISK with segmented bars
    metrics(o, s, 26, 15, true);

    // Divider with a chamfered notch
    drawHLinePortrait(o, 0, 89, 24);
    for (int i = 0; i < 4; ++i) setPortraitPixel(o, 24 + i, 89 - i, true);
    drawHLinePortrait(o, 28, 86, 4);

    ipRow(o, 92, s);
    std::string host = hostTag();
    drawTextColor(o, (PW - textWidth(host)) / 2, 103, host, true);

    // Status row: current throttle code > since-boot marker > OK
    if (s.throttled) {
        const char* why = s.thr_now ? "THR" : s.cap_now ? "CAP" : s.temp_now ? "TMP" : "UV";
        tagBox(o, 0, 113, "H");
        valueRight(o, 114, why);
    } else if (s.uv_boot || s.cap_boot || s.thr_boot || s.temp_boot) {
        fillRect(o, 0, 113, PW, 9);
        std::string m = s.uv_boot ? "!UV" : "!TH";
        drawTextColor(o, (PW - textWidth(m)) / 2, 114, m, false);
    } else {
        drawTextColor(o, 0, 114, "[", true);
        drawTextColor(o, PW - 5, 114, "]", true);
        drawTextColor(o, (PW - textWidth("OK")) / 2, 114, "OK", true);
    }
    hazardStripes(o, 124, 4);
}

void drawEvaAlertScreen(SSD1306& o, const Stats& s, long activeSecs, int events) {
    // Header: 電圧 / 低下 (voltage drop), cut out of a solid block
    fillRect(o, 0, 0, PW, 33);
    drawKanji(o, 0, 0, K_DEN, false);
    drawKanji(o, 16, 0, K_ATSU, false);
    drawKanji(o, 0, 17, K_TEI, false);
    drawKanji(o, 16, 17, K_KA, false);
    int phase = int(activeSecs % 6);
    hazardStripes(o, 35, 5, phase);

    // Bolt + LOW + time in alert
    drawBoltPortrait(o, 0, 43, 1);
    drawTextColor(o, PW - textWidth("LOW"), 42, "LOW", true);
    std::string d = fmtDuration(activeSecs);
    drawTextColor(o, PW - textWidth(d), 50, d, true);
    drawHLinePortrait(o, 0, 59, PW);

    // Compact metrics, no bars
    metrics(o, s, 62, 10, false);
    drawHLinePortrait(o, 0, 102, PW);

    ipRow(o, 104, s);
    char n[16]; std::snprintf(n, sizeof(n), "N:%d", events);
    drawTextColor(o, 0, 115, n, true);
    hazardStripes(o, 124, 4, phase);
}
