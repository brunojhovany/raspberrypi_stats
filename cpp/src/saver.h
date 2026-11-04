#pragma once
#include "ssd1306.h"

// Screensaver: Eva-style "システム状態 / SYSTEM STATUS" banner drifting up the
// 32x128 portrait canvas. One call per animation frame; frame keeps counting.
// sticky=true inverts the SYSTEM STATUS strip (something happened since boot).
void drawSaverFrame(SSD1306& oled, long frame, bool sticky);
