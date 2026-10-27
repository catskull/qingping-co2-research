// Renders the CO2 display UI with sample readings and writes raw RGB888 frames for png.py.
// Usage: screenshot <out-prefix>   -> <out-prefix>-<theme>.rgb (320x170)
#include "../../src/lcd_ui.h"

#include <utility>

#include "../../.pio/libdeps/tdisplay/GFX Library for Arduino/src/font/glcdfont.h"

uint32_t millis() {
    return 0;  // blink phase "on"
}

void Arduino_GFX::fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c) {
    using std::swap;
    if (y0 > y1) { swap(y0, y1); swap(x0, x1); }
    if (y1 > y2) { swap(y2, y1); swap(x2, x1); }
    if (y0 > y1) { swap(y0, y1); swap(x0, x1); }
    int a, b, y, last;
    if (y0 == y2) {
        a = b = x0;
        a = min({a, x1, x2});
        b = max({b, x1, x2});
        drawFastHLine(a, y0, b - a + 1, c);
        return;
    }
    int dx01 = x1 - x0, dy01 = y1 - y0, dx02 = x2 - x0, dy02 = y2 - y0, dx12 = x2 - x1, dy12 = y2 - y1;
    int32_t sa = 0, sb = 0;
    last = (y1 == y2) ? y1 : y1 - 1;
    for (y = y0; y <= last; y++) {
        a = x0 + sa / dy01;
        b = x0 + sb / dy02;
        sa += dx01;
        sb += dx02;
        if (a > b) swap(a, b);
        drawFastHLine(a, y, b - a + 1, c);
    }
    sa = (int32_t)dx12 * (y - y1);
    sb = (int32_t)dx02 * (y - y0);
    for (; y <= y2; y++) {
        a = x1 + sa / dy12;
        b = x0 + sb / dy02;
        sa += dx12;
        sb += dx02;
        if (a > b) swap(a, b);
        drawFastHLine(a, y, b - a + 1, c);
    }
}

void Arduino_GFX::print(const char *s) {
    for (; *s; s++) {
        unsigned char ch = *s;
        for (int i = 0; i < 5; i++) {
            uint8_t line = font[ch * 5 + i];
            for (int j = 0; j < 8; j++, line >>= 1) {
                if (line & 1) {
                    fillRect(cx + i * textSize, cy + j * textSize, textSize, textSize, textColor);
                }
            }
        }
        cx += 6 * textSize;
    }
}

int main(int argc, char **argv) {
    const char *prefix = argc > 1 ? argv[1] : "display";
    static Arduino_GFX gfx;
    uiInit(&gfx);

    UiState s = {};
    s.hasData = true;
    s.rxFlash = true;
    s.name = "QP-4D7D";
    s.rssi = -62;
    s.ageS = 2;
    s.reading.hasCo2 = true;
    s.reading.co2 = 742;
    s.reading.hasTemp = true;
    s.reading.tempC = 21.4f;
    s.reading.hasHumidity = true;
    s.reading.humidity = 41;

    for (int t = 0; t < uiThemeCount(); t++) {
        uiSetTheme(t);
        uiRender(s);
        char path[256];
        snprintf(path, sizeof(path), "%s-%s.rgb", prefix, uiThemeName(t));
        FILE *f = fopen(path, "wb");
        for (uint16_t px : gfx.fb) {
            // expand RGB565 the way the panel does: replicate high bits into the low ones
            uint8_t r = (px >> 11) & 0x1F, g = (px >> 5) & 0x3F, b = px & 0x1F;
            uint8_t rgb[3] = {(uint8_t)(r << 3 | r >> 2), (uint8_t)(g << 2 | g >> 4), (uint8_t)(b << 3 | b >> 2)};
            fwrite(rgb, 1, 3, f);
        }
        fclose(f);
        printf("%s\n", path);
    }
}
