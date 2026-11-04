#pragma once
#include "ssd1306.h"
#include "stats.h"

// Evangelion-style alternate layout (portrait 32x128, monochrome).
// Selected with RPI_STATS_VIEW=eva or RPI_STATS_VIEW=cycle.
void drawEvaScreen(SSD1306& oled, const Stats& s, int counter);
void drawEvaAlertScreen(SSD1306& oled, const Stats& s, long activeSecs, int events);
