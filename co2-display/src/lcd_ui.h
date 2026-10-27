// Late-90s monochrome LCD look: seven-segment digits with ghost segments, fixed annunciators,
// segmented bar graph and a 5x7 pixel font, in a few backlight/glass themes.
#pragma once

#include <Arduino_GFX_Library.h>

#include "bthome.h"

struct UiState {
    bool hasData;      // any packet received since boot
    bool stale;        // no packet for a while
    bool rxFlash;      // a packet arrived moments ago
    const char *name;  // sender, e.g. "QP-4D7D"
    int rssi;
    uint32_t ageS;
    BthomeReading reading;
    int previewDigit = -1;  // DIGIT_PREVIEW builds: 0-9 fills the CO2 readout with that digit, 10 = dashes
};

void uiInit(Arduino_GFX *gfx);
int uiThemeCount();
void uiSetTheme(int index);
const char *uiThemeName(int index);
void uiRender(const UiState &s);
