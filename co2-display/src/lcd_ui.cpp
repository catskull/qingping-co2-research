#include "lcd_ui.h"

#include <math.h>

struct Rgb {
    uint8_t r, g, b;
};

struct Theme {
    const char *name;
    Rgb bezel;
    Rgb glassEdge;    // glass colour at top/bottom
    Rgb glassCenter;  // brighter middle, like an uneven backlight
    Rgb ink;          // lit segments / printed legends
    Rgb ghost;        // unlit segments, faintly visible
};

static const Theme THEMES[] = {
    // positive STN, dark segments on yellow-green (synth / rack gear)
    {"STN", {16, 18, 14}, {186, 198, 128}, {198, 210, 140}, {32, 40, 22}, {178, 190, 122}},
    // negative amber (car stereo)
    {"AMBER", {6, 6, 6}, {14, 8, 2}, {30, 18, 5}, {255, 158, 36}, {37, 22, 6}},
    // negative ice blue
    {"ICE", {6, 6, 8}, {3, 8, 14}, {8, 18, 30}, {112, 220, 255}, {11, 24, 37}},
    // negative red
    {"RED", {6, 6, 6}, {12, 3, 3}, {26, 6, 5}, {255, 62, 40}, {33, 8, 7}},
    // negative green
    {"GREEN", {6, 6, 6}, {3, 10, 4}, {7, 22, 10}, {96, 255, 128}, {9, 28, 12}},
};
static const int NUM_THEMES = sizeof(THEMES) / sizeof(THEMES[0]);

static Arduino_GFX *gfx = nullptr;
static const Theme *theme = &THEMES[0];
static uint16_t inkColor, ghostColor;
static uint16_t mainGhostColor;  // fainter ghosts for the large CO2 digits

// --- geometry ---
static const int GLASS_X = 6, GLASS_Y = 6, GLASS_W = 308, GLASS_H = 158, GLASS_R = 8;
static const float SKEW = 0.0f;  // italic lean of segments; any lean aliases into stair steps

static const uint16_t BAR_MIN = 400, BAR_MAX = 2000;
static const uint16_t CO2_GOOD = 800, CO2_FAIR = 1200;  // same thresholds as the monitor's LED

static uint16_t toColor(Rgb c) {
    return RGB565(c.r, c.g, c.b);
}

static Rgb lerp(Rgb a, Rgb b, float t) {
    return {(uint8_t)(a.r + (b.r - a.r) * t), (uint8_t)(a.g + (b.g - a.g) * t), (uint8_t)(a.b + (b.b - a.b) * t)};
}

void uiInit(Arduino_GFX *g) {
    gfx = g;
    uiSetTheme(0);
}

int uiThemeCount() {
    return NUM_THEMES;
}

const char *uiThemeName(int index) {
    return THEMES[index % NUM_THEMES].name;
}

void uiSetTheme(int index) {
    theme = &THEMES[((index % NUM_THEMES) + NUM_THEMES) % NUM_THEMES];
    inkColor = toColor(theme->ink);
    ghostColor = toColor(theme->ghost);
    mainGhostColor = toColor(lerp(theme->ghost, theme->glassCenter, 0.5f));
}

// Bezel plus glass with a vertical backlight gradient and rounded corners.
static void drawGlass() {
    gfx->fillScreen(toColor(theme->bezel));
    for (int dy = 0; dy < GLASS_H; dy++) {
        float t = 1.0f - fabsf((dy - GLASS_H / 2.0f) / (GLASS_H / 2.0f));  // 0 at edges, 1 in the middle
        uint16_t c = toColor(lerp(theme->glassEdge, theme->glassCenter, t));
        int inset = 0;
        int fromEdge = min(dy, GLASS_H - 1 - dy);
        if (fromEdge < GLASS_R) {
            float d = GLASS_R - fromEdge - 0.5f;
            inset = (int)lroundf(GLASS_R - sqrtf(GLASS_R * GLASS_R - d * d));
        }
        gfx->drawFastHLine(GLASS_X + inset, GLASS_Y + dy, GLASS_W - 2 * inset, c);
    }
}

// --- seven-segment rendering ---

struct Pt {
    float x, y;
};

static void fillConvex(const Pt *p, int n, uint16_t color) {
    for (int i = 1; i + 1 < n; i++) {
        gfx->fillTriangle(lroundf(p[0].x), lroundf(p[0].y), lroundf(p[i].x), lroundf(p[i].y), lroundf(p[i + 1].x),
                          lroundf(p[i + 1].y), color);
    }
}

// Shear points so the shape leans right, pivoting on the cell's bottom edge.
static void shear(Pt *p, int n, float baseY) {
    for (int i = 0; i < n; i++) {
        p[i].x += (baseY - p[i].y) * SKEW;
    }
}

static void hSegment(float x1, float x2, float yc, float t, float baseY, uint16_t color) {
    float h = t / 2;
    Pt p[6] = {{x1, yc}, {x1 + h, yc - h}, {x2 - h, yc - h}, {x2, yc}, {x2 - h, yc + h}, {x1 + h, yc + h}};
    shear(p, 6, baseY);
    fillConvex(p, 6, color);
}

static void vSegment(float xc, float y1, float y2, float t, float baseY, uint16_t color) {
    float h = t / 2;
    Pt p[6] = {{xc, y1}, {xc + h, y1 + h}, {xc + h, y2 - h}, {xc, y2}, {xc - h, y2 - h}, {xc - h, y1 + h}};
    shear(p, 6, baseY);
    fillConvex(p, 6, color);
}

// bit 0..6 = segments a..g
static const uint8_t DIGIT_MASKS[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};
static const uint8_t MASK_DASH = 0x40;
static const uint8_t MASK_BLANK = 0x00;

static void drawDigit(int x, int y, int w, int h, int t, uint8_t mask) {
    float g = t * 0.18f;  // small gap between segments, like real LCD glass
    float half = t / 2.0f;
    float top = y + half, mid = y + h / 2.0f, bot = y + h - half;
    float left = x + half, right = x + w - half;
    float base = y + h;
    auto col = [&](int seg) { return (mask >> seg) & 1 ? inkColor : ghostColor; };

    hSegment(left + g, right - g, top, t, base, col(0));     // a
    vSegment(right, top + g, mid - g, t, base, col(1));      // b
    vSegment(right, mid + g, bot - g, t, base, col(2));      // c
    hSegment(left + g, right - g, bot, t, base, col(3));     // d
    vSegment(left, mid + g, bot - g, t, base, col(4));       // e
    vSegment(left, top + g, mid - g, t, base, col(5));       // f
    hSegment(left + g, right - g, mid, t, base, col(6));     // g
}

static void drawDecimalPoint(int x, int y, int size, bool on) {
    gfx->fillRect(x, y - size, size, size, on ? inkColor : ghostColor);
}

// Right-aligned number in `digits` cells; leading zeros blank (ghost only). `masks` overrides when set.
static void drawNumber(int x, int y, int w, int h, int t, int spacing, int digits, long value, const uint8_t *masks) {
    for (int i = digits - 1; i >= 0; i--) {
        uint8_t m;
        if (masks) {
            m = masks[i];
        } else if (value > 0 || i == digits - 1) {
            m = DIGIT_MASKS[value % 10];
            value /= 10;
        } else {
            m = MASK_BLANK;
        }
        drawDigit(x + i * (w + spacing), y, w, h, t, m);
    }
}

// --- sixteen-segment rendering (main readout) ---

enum : uint16_t {
    S_A1 = 1 << 0,   // top, left half
    S_A2 = 1 << 1,   // top, right half
    S_B = 1 << 2,    // upper right
    S_C = 1 << 3,    // lower right
    S_D1 = 1 << 4,   // bottom, left half
    S_D2 = 1 << 5,   // bottom, right half
    S_E = 1 << 6,    // lower left
    S_F = 1 << 7,    // upper left
    S_G1 = 1 << 8,   // middle, left half
    S_G2 = 1 << 9,   // middle, right half
    S_H = 1 << 10,   // upper-left diagonal
    S_I = 1 << 11,   // upper centre vertical
    S_J = 1 << 12,   // upper-right diagonal
    S_K = 1 << 13,   // lower-left diagonal
    S_L = 1 << 14,   // lower centre vertical
    S_M = 1 << 15,   // lower-right diagonal
};
static const uint16_t S_A = S_A1 | S_A2;
static const uint16_t S_D = S_D1 | S_D2;

static const uint16_t DIGIT16[10] = {
    S_A | S_B | S_C | S_D | S_E | S_F,                // 0
    S_A1 | S_I | S_L | S_D,                           // 1: centred stem, flag top-left, full base
    S_A | S_B | S_D | S_E | S_G1 | S_G2,              // 2
    S_A | S_B | S_C | S_D | S_G2,                     // 3
    S_B | S_C | S_F | S_G1 | S_G2,                    // 4
    S_A | S_C | S_D | S_F | S_G1 | S_G2,              // 5
    S_A | S_C | S_D | S_E | S_F | S_G1 | S_G2,        // 6
    S_A | S_J | S_L,                                  // 7: diagonal into a centre stem
    S_A | S_B | S_C | S_D | S_E | S_F | S_G1 | S_G2,  // 8
    S_A | S_B | S_C | S_D | S_F | S_G1 | S_G2,        // 9
};
static const uint16_t DASH16 = S_G1 | S_G2;
static const uint16_t BLANK16 = 0;

// Thick diagonal bar filling the box corner to corner; `rising` goes bottom-left to top-right.
static void diagSegment(float x0, float y0, float x1, float y1, float t, bool rising, uint16_t color) {
    float tw = min(t * 0.9f, x1 - x0), th = min(t * 0.9f, y1 - y0);
    Pt p[6] = {{x0, y0}, {x0 + tw, y0}, {x1, y1 - th}, {x1, y1}, {x1 - tw, y1}, {x0, y0 + th}};
    if (rising) {
        for (Pt &q : p) {
            q.x = x0 + x1 - q.x;
        }
    }
    fillConvex(p, 6, color);
}

// Half of a split horizontal bar: pointed at the outer end, flat where it meets its partner,
// so the two halves read as one continuous stroke.
static void halfHSegment(float outer, float joint, float yc, float t, float baseY, uint16_t color) {
    float h = t / 2;
    float dir = joint > outer ? 1 : -1;  // from the outer end towards the joint
    Pt p[5] = {{outer, yc}, {outer + dir * h, yc - h}, {joint, yc - h}, {joint, yc + h}, {outer + dir * h, yc + h}};
    shear(p, 5, baseY);
    fillConvex(p, 5, color);
}

static void drawDigit16(int x, int y, int w, int h, int t, uint16_t mask) {
    static const float JOINT_GAP = 1.0f;  // each side of centre where split bars meet
    float g = max(1.5f, t * 0.25f);
    float half = t / 2.0f;
    float top = y + half, mid = y + h / 2.0f, bot = y + h - half;
    float left = x + half, right = x + w - half, cx = x + w / 2.0f;
    float base = y + h;
    float inL = left + half + g, inR = right - half - g;
    float inCL = cx - half - g, inCR = cx + half + g;
    float upT = top + half + g, upB = mid - half - g;
    float loT = mid + half + g, loB = bot - half - g;

    // Ghosts first, lit segments on top, so overlapping tips never dim a lit segment.
    for (int pass = 0; pass < 2; pass++) {
        bool litPass = pass == 1;
        uint16_t c = litPass ? inkColor : mainGhostColor;
        auto want = [&](uint16_t seg) { return ((mask & seg) != 0) == litPass; };

        if (want(S_A1)) halfHSegment(left + g, cx - JOINT_GAP, top, t, base, c);
        if (want(S_A2)) halfHSegment(right - g, cx + JOINT_GAP, top, t, base, c);
        if (want(S_B)) vSegment(right, top + g, mid - g, t, base, c);
        if (want(S_C)) vSegment(right, mid + g, bot - g, t, base, c);
        if (want(S_D1)) halfHSegment(left + g, cx - JOINT_GAP, bot, t, base, c);
        if (want(S_D2)) halfHSegment(right - g, cx + JOINT_GAP, bot, t, base, c);
        if (want(S_E)) vSegment(left, mid + g, bot - g, t, base, c);
        if (want(S_F)) vSegment(left, top + g, mid - g, t, base, c);
        if (want(S_G1)) halfHSegment(left + g, cx - JOINT_GAP, mid, t, base, c);
        if (want(S_G2)) halfHSegment(right - g, cx + JOINT_GAP, mid, t, base, c);
        if (want(S_I)) vSegment(cx, top + g, mid - g, t, base, c);
        if (want(S_L)) vSegment(cx, mid + g, bot - g, t, base, c);
        if (want(S_H)) diagSegment(inL, upT, inCL, upB, t, false, c);
        if (want(S_J)) diagSegment(inCR, upT, inR, upB, t, true, c);
        if (want(S_K)) diagSegment(inL, loT, inCL, loB, t, true, c);
        if (want(S_M)) diagSegment(inCR, loT, inR, loB, t, false, c);
    }
}

// Right-aligned number on 16-segment digits; leading zeros blank. `masks` overrides when set.
static void drawNumber16(int x, int y, int w, int h, int t, int spacing, int digits, long value,
                         const uint16_t *masks) {
    for (int i = digits - 1; i >= 0; i--) {
        uint16_t m;
        if (masks) {
            m = masks[i];
        } else if (value > 0 || i == digits - 1) {
            m = DIGIT16[value % 10];
            value /= 10;
        } else {
            m = BLANK16;
        }
        drawDigit16(x + i * (w + spacing), y, w, h, t, m);
    }
}

// --- small elements ---

static void text(int x, int y, int size, const char *s, bool lit = true) {
    gfx->setTextSize(size);
    gfx->setTextColor(lit ? inkColor : ghostColor);
    gfx->setCursor(x, y);
    gfx->print(s);
}

static void signalBars(int x, int bottom, int rssi, bool show) {
    int bars = !show ? 0 : rssi >= -60 ? 4 : rssi >= -70 ? 3 : rssi >= -80 ? 2 : rssi >= -90 ? 1 : 0;
    for (int i = 0; i < 4; i++) {
        int h = 3 + i * 2;
        gfx->fillRect(x + i * 6, bottom - h, 4, h, i < bars ? inkColor : ghostColor);
    }
}

static int barX(uint16_t ppm, int x0, int x1) {
    ppm = constrain(ppm, BAR_MIN, BAR_MAX);
    return x0 + (long)(ppm - BAR_MIN) * (x1 - x0) / (BAR_MAX - BAR_MIN);
}

static void barGraph(int x0, int x1, int y, int h, uint16_t ppm, bool show) {
    const int segs = 24;
    const float gap = 2.5f;
    float segW = ((x1 - x0) - gap * (segs - 1)) / segs;
    int lit = show ? (int)ceilf((float)(constrain(ppm, BAR_MIN, BAR_MAX) - BAR_MIN) / (BAR_MAX - BAR_MIN) * segs) : 0;
    for (int i = 0; i < segs; i++) {
        float sx = x0 + i * (segW + gap);
        Pt p[4] = {{sx, (float)y}, {sx + segW, (float)y}, {sx + segW, (float)(y + h)}, {sx, (float)(y + h)}};
        shear(p, 4, y + h);
        fillConvex(p, 4, i < lit ? inkColor : ghostColor);
    }
    // printed legend under the bar
    static const uint16_t ticks[] = {400, 800, 1200, 1600, 2000};
    gfx->setTextSize(1);
    gfx->setTextColor(inkColor);
    for (uint16_t v : ticks) {
        char buf[6];
        snprintf(buf, sizeof(buf), "%u", v);
        int tw = strlen(buf) * 6 - 1;
        int tx = constrain(barX(v, x0, x1) - tw / 2, x0, x1 - tw);
        gfx->setCursor(tx, y + h + 4);
        gfx->print(buf);
        gfx->drawFastVLine(barX(v, x0, x1), y + h + 1, 2, inkColor);
    }
}

// --- screen ---

void uiRender(const UiState &s) {
    uint32_t now = millis();
    bool blinkOn = (now / 500) % 2 == 0;
    bool showValues = s.hasData && !(s.stale && !blinkOn);

    drawGlass();

    // top row: sender, age, RX annunciator, signal bars
    char buf[32];
    snprintf(buf, sizeof(buf), "%s", s.hasData ? s.name : "QP-----");
    text(16, 13, 1, buf);
    if (s.hasData) {
        snprintf(buf, sizeof(buf), "%lus", (unsigned long)s.ageS);
        text(70, 13, 1, buf);
    }
    text(228, 13, 1, "RX", s.rxFlash);
    signalBars(250, 21, s.rssi, s.hasData && !s.stale);
    text(278, 13, 1, theme->name);

    // main CO2 readout
    static const uint16_t dashes16[4] = {DASH16, DASH16, DASH16, DASH16};
    static const uint16_t blanks16[4] = {BLANK16, BLANK16, BLANK16, BLANK16};
    static const uint8_t blanks[4] = {MASK_BLANK, MASK_BLANK, MASK_BLANK, MASK_BLANK};
    const uint16_t *override = !s.hasData ? dashes16 : !showValues ? blanks16 : nullptr;
    uint16_t previewMasks[4];
    if (s.previewDigit >= 0) {
        for (uint16_t &m : previewMasks) {
            m = s.previewDigit < 10 ? DIGIT16[s.previewDigit] : DASH16;
        }
        override = previewMasks;
    }
    drawNumber16(16, 26, 50, 76, 6, 8, 4, min<long>(s.reading.co2, 9999), override);

    text(246, 30, 2, "CO2");
    text(246, 50, 1, "ppm");
    uint16_t co2 = s.reading.co2;
    bool live = s.hasData && !s.stale;
    text(246, 66, 1, "GOOD", live && co2 < CO2_GOOD);
    text(246, 77, 1, "FAIR", live && co2 >= CO2_GOOD && co2 < CO2_FAIR);
    text(246, 88, 1, "POOR", live && co2 >= CO2_FAIR);

    barGraph(16, 304, 108, 9, co2, showValues);

    // bottom row: temperature and humidity on small seven-segment digits
    text(16, 142, 1, "TEMP");
    bool tempDp = false;
    if (showValues && s.reading.hasTemp) {
        float f = s.reading.tempC * 9.0f / 5.0f + 32.0f;
        long tenths = lroundf(f * 10);
        uint8_t m[3];
        if (tenths < 0) {  // "-NN" whole degrees
            long v = min(lroundf(-f), 99L);
            m[0] = MASK_DASH;
            m[1] = DIGIT_MASKS[v / 10];
            m[2] = DIGIT_MASKS[v % 10];
        } else if (tenths >= 1000) {  // "NNN" whole degrees
            long v = min(lroundf(f), 999L);
            m[0] = DIGIT_MASKS[v / 100];
            m[1] = DIGIT_MASKS[(v / 10) % 10];
            m[2] = DIGIT_MASKS[v % 10];
        } else {  // "NN.N"
            m[0] = tenths >= 100 ? DIGIT_MASKS[tenths / 100] : MASK_BLANK;
            m[1] = DIGIT_MASKS[(tenths / 10) % 10];
            m[2] = DIGIT_MASKS[tenths % 10];
            tempDp = true;
        }
        drawNumber(46, 132, 15, 26, 4, 5, 3, 0, m);
    } else {
        drawNumber(46, 132, 15, 26, 4, 5, 3, 0, blanks);
    }
    drawDecimalPoint(46 + 2 * 20 - 4, 158, 3, tempDp);
    gfx->drawRect(106, 150, 3, 3, inkColor);  // degree sign; the built-in font has none
    text(111, 150, 1, "F");

    text(140, 142, 1, "HUMI");
    if (showValues && s.reading.hasHumidity) {
        drawNumber(170, 132, 15, 26, 4, 5, 2, min(lroundf(s.reading.humidity), 99L), nullptr);
    } else {
        drawNumber(170, 132, 15, 26, 4, 5, 2, 0, blanks);
    }
    text(210, 150, 1, "%");

    // status annunciators
    text(248, 136, 1, "SEARCH", !s.hasData && blinkOn);
    text(248, 148, 1, "STALE", s.stale);
}
