// Drawing primitives for the 32x128 portrait layout (device is 128x32, rotated)
#include "portrait.h"
#include <cmath>
#include <cstdio>
#include <vector>

// Bring font locally for portrait text drawing
static const uint8_t font5x7_portrait[] = {
#include "tiny5x7.inc"
};

static int g_shiftY = 0;
void setPortraitShift(int dy) { g_shiftY = dy; }

static const uint16_t kKanji16[] = {
#include "kanji16.inc"
};

void drawKanji(SSD1306& oled, int x, int y, int idx, bool on) {
    const uint16_t* g = &kKanji16[idx * 16];
    for (int r = 0; r < 16; ++r)
        for (int c = 0; c < 16; ++c)
            if (g[r] & (0x8000u >> c)) setPortraitPixel(oled, x + c, y + r, on);
}

void setPortraitPixel(SSD1306& oled, int xp, int yp, bool on) {
    yp += g_shiftY;
    // Portrait buffer is 32x128; rotate CCW into 128x32 device buffer
    // Mapping: xd = yp, yd = 31 - xp
    int xd = yp;
    int yd = 31 - xp;
    oled.setPixel(xd, yd, on);
}

void drawHLinePortrait(SSD1306& oled, int x, int y, int w, bool on) {
    for (int i = 0; i < w; ++i) setPortraitPixel(oled, x + i, y, on);
}

void drawTextPortrait(SSD1306& oled, int x, int y, const std::string& text) {
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

void drawCenteredTextPortrait(SSD1306& oled, int y, const std::string& text) {
    int w = static_cast<int>(text.size()) * 6; // approx
    int x = (32 - w) / 2;
    if (x < 0) x = 0;
    drawTextPortrait(oled, x, y, text);
}

// --- Scaled text (integer scaling of 5x7 font) to fit width 32 ---
void drawScaledTextPortrait(SSD1306& oled, int x, int y, const std::string& text, int scale) {
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

void drawScaledTextPortraitCentered(SSD1306& oled, int y, const std::string& text) {
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
void drawIPProportional(SSD1306& oled, const std::string& text, int topY, int heightAvail) {
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

void drawDonutPortrait(SSD1306& oled, int cx, int cy, int rOuter, int rInner, int percent) {
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

// 10x12 lightning bolt, drawn scaled for the under-voltage screen
static const char* kBolt[] = {
    "......####",
    ".....####.",
    "....####..",
    "...####...",
    "..####....",
    ".#########",
    "#########.",
    "....####..",
    "...####...",
    "..####....",
    ".####.....",
    "####......",
};

void drawBoltPortrait(SSD1306& oled, int x, int y, int scale) {
    for (int r = 0; r < 12; ++r)
        for (int col = 0; kBolt[r][col]; ++col)
            if (kBolt[r][col] == '#')
                for (int dx = 0; dx < scale; ++dx)
                    for (int dy = 0; dy < scale; ++dy)
                        setPortraitPixel(oled, x + col*scale + dx, y + r*scale + dy, true);
}

std::string fmtDuration(long secs) {
    char b[24];
    if (secs < 100) std::snprintf(b, sizeof(b), "%lds", secs);
    else if (secs < 6000) std::snprintf(b, sizeof(b), "%ldm", secs / 60);
    else std::snprintf(b, sizeof(b), "%ldh", secs / 3600);
    return b;
}

const uint8_t* glyph5x7(char ch) {
    uint8_t c = static_cast<uint8_t>(ch);
    if (c < 32 || c > 127) c = '?';
    return &font5x7_portrait[(c - 32) * 5];
}
