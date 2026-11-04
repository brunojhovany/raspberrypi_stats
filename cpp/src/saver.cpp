#include "saver.h"
#include "portrait.h"
#include <string>

// Banner geometry (portrait coords relative to its top edge)
static constexpr int kGlyphTop = 3;
static constexpr int kBannerH = kGlyphTop + 6 * 16 + 3;   // 6 glyphs of 16 px + frame
static constexpr int kTravel = PH + kBannerH + 10;        // full pass incl. a blank gap

// 5x7 text rotated 90 deg clockwise: reads top-to-bottom, like a book spine
static void drawTextSpine(SSD1306& o, int x, int y, const std::string& t, bool on) {
    for (char ch : t) {
        const uint8_t* g = glyph5x7(ch);
        for (int c = 0; c < 5; ++c)
            for (int r = 0; r < 7; ++r)
                if (g[c] & (1u << r)) setPortraitPixel(o, x + 6 - r, y + c, on);
        y += 6;
    }
}

static void fill(SSD1306& o, int x, int y, int w, int h, bool on = true) {
    for (int dy = 0; dy < h; ++dy)
        for (int dx = 0; dx < w; ++dx) setPortraitPixel(o, x + dx, y + dy, on);
}

// Diagonal stripes inside a rectangle, anchored to the banner so they move with it
static void stripes(SSD1306& o, int x, int y, int w, int h) {
    for (int dy = 0; dy < h; ++dy)
        for (int dx = 0; dx < w; ++dx)
            if (((dx + dy) % 6) < 3) setPortraitPixel(o, x + dx, y + dy, true);
}

static void drawBanner(SSD1306& o, int by, bool mirror, bool sticky) {
    // Column layout (x): kanji 16 | gap | rule | gap | spine text 7 | gap | stripes 5
    int xK = mirror ? 16 : 0;
    int xRule = mirror ? 14 : 17;
    int xText = mirror ? 6 : 19;
    int xStripe = mirror ? 0 : 27;
    const int body = 6 * 16;

    // Frame: top/bottom rules with a chamfered corner on the kanji side
    drawHLinePortrait(o, 0, by, PW);
    drawHLinePortrait(o, 0, by + kBannerH - 1, PW);
    int cx = mirror ? PW - 1 : 0;
    for (int i = 1; i <= 3; ++i) setPortraitPixel(o, cx, by + i, true);

    // システム状態, written vertically (tategaki)
    static const int glyphs[6] = {K_SHI, K_SU, K_TE, K_MU, K_JO, K_TAI};
    for (int i = 0; i < 6; ++i) drawKanji(o, xK, by + kGlyphTop + i * 16, glyphs[i], true);

    // Separator rule
    for (int y = by + kGlyphTop; y < by + kGlyphTop + body; ++y) setPortraitPixel(o, xRule, y, true);

    // SYSTEM STATUS along the spine, inverted when something happened since boot
    const std::string label = "SYSTEM STATUS";
    int textLen = int(label.size()) * 6;
    if (sticky) fill(o, xText - 1, by + kGlyphTop - 1, 9, textLen + 1);
    drawTextSpine(o, xText, by + kGlyphTop, label, !sticky);
    stripes(o, xText, by + kGlyphTop + textLen + 3, 7, body - textLen - 3);

    // Hazard stripe column
    stripes(o, xStripe, by + kGlyphTop, 5, body);
}

void drawSaverFrame(SSD1306& o, long frame, bool sticky) {
    long pass = frame / kTravel;
    int by = PH - int(frame % kTravel);   // enters at the bottom, drifts upward
    drawBanner(o, by, pass % 2 == 1, sticky);  // swap sides every pass to spread wear
}
