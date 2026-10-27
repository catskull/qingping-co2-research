// Host-side stand-in for the parts of Arduino_GFX that lcd_ui.cpp uses, drawing into an RGB565
// framebuffer so the real renderer can be screenshotted on a desktop machine.
#pragma once

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <cmath>

using std::max;
using std::min;

#define RGB565(r, g, b) ((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))

uint32_t millis();

#define constrain(x, lo, hi) ((x) < (lo) ? (lo) : ((x) > (hi) ? (hi) : (x)))

class Arduino_GFX {
public:
    static const int W = 320, H = 170;
    uint16_t fb[W * H] = {};

    void drawPixel(int x, int y, uint16_t c) {
        if (x >= 0 && y >= 0 && x < W && y < H) {
            fb[y * W + x] = c;
        }
    }
    void fillRect(int x, int y, int w, int h, uint16_t c) {
        for (int j = y; j < y + h; j++) {
            for (int i = x; i < x + w; i++) {
                drawPixel(i, j, c);
            }
        }
    }
    void fillScreen(uint16_t c) { fillRect(0, 0, W, H, c); }
    void drawFastHLine(int x, int y, int w, uint16_t c) { fillRect(x, y, w, 1, c); }
    void drawFastVLine(int x, int y, int h, uint16_t c) { fillRect(x, y, 1, h, c); }
    void drawRect(int x, int y, int w, int h, uint16_t c) {
        drawFastHLine(x, y, w, c);
        drawFastHLine(x, y + h - 1, w, c);
        drawFastVLine(x, y, h, c);
        drawFastVLine(x + w - 1, y, h, c);
    }
    void fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c);  // Adafruit_GFX algorithm

    void setTextSize(uint8_t s) { textSize = s; }
    void setTextColor(uint16_t c) { textColor = c; }
    void setCursor(int x, int y) { cx = x; cy = y; }
    void print(const char *s);

private:
    uint8_t textSize = 1;
    uint16_t textColor = 0xFFFF;
    int cx = 0, cy = 0;
};
