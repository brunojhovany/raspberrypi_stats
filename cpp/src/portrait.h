#pragma once
#include "ssd1306.h"
#include <string>

// Logical portrait canvas: 32 px wide x 128 px tall
constexpr int PW = 32;
constexpr int PH = 128;

void setPortraitPixel(SSD1306& oled, int xp, int yp, bool on);
void drawHLinePortrait(SSD1306& oled, int x, int y, int w, bool on = true);
void drawTextPortrait(SSD1306& oled, int x, int y, const std::string& text);
void drawCenteredTextPortrait(SSD1306& oled, int y, const std::string& text);
void drawScaledTextPortrait(SSD1306& oled, int x, int y, const std::string& text, int scale);
void drawScaledTextPortraitCentered(SSD1306& oled, int y, const std::string& text);
void drawIPProportional(SSD1306& oled, const std::string& text, int topY, int heightAvail);
void drawDonutPortrait(SSD1306& oled, int cx, int cy, int rOuter, int rInner, int percent);
void drawBoltPortrait(SSD1306& oled, int x, int y, int scale);
std::string fmtDuration(long secs);

// Vertical pixel shift applied to everything drawn (burn-in protection).
// Pixels pushed past the bottom are clipped.
void setPortraitShift(int dy);

// 16x16 glyphs from kanji16.inc (order of GLYPHS in tools/gen_kanji.py)
enum KanjiIdx { K_SEI, K_JOU, K_DEN, K_ATSU, K_TEI, K_KA,   // 正常 電圧 低下
                K_SHI, K_SU, K_TE, K_MU, K_JO, K_TAI };      // システム 状態
void drawKanji(SSD1306& oled, int x, int y, int idx, bool on);

// 5 column bytes of the 5x7 glyph for c (bit n = row n); '?' for non-ASCII
const uint8_t* glyph5x7(char c);
