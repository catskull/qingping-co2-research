// CO2 display for the LilyGO T-Display-S3: listens for BTHome BLE advertisements from the
// Qingping monitor firmware ("QP-xxxx") and shows the latest CO2, temperature and humidity
// in a late-90s monochrome LCD style. GPIO14 cycles colour themes; GPIO0 flips the display 180 degrees.
#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <NimBLEDevice.h>
#include <Preferences.h>

#include "bthome.h"
#include "lcd_ui.h"

// T-Display-S3: ST7789 170x320 on an 8-bit parallel bus
static const int PIN_LCD_POWER = 15;  // must be high when running from battery
static const int PIN_LCD_BL = 38;
static const int PIN_LCD_DC = 7;
static const int PIN_LCD_CS = 6;
static const int PIN_LCD_WR = 8;
static const int PIN_LCD_RD = 9;
static const int PIN_LCD_RST = 5;
static const int PIN_BUTTON_THEME = 14;   // active-low
static const int PIN_BUTTON_ROTATE = 0;   // BOOT button, usable as a normal input once running

// Panel rotations for landscape and landscape flipped 180 degrees
static const uint8_t ROTATION_NORMAL = 1;
static const uint8_t ROTATION_FLIPPED = 3;

static const char NAME_PREFIX[] = "QP-";
static const uint32_t STALE_MS = 30000;  // monitor broadcasts every ~5 s
static const uint32_t RX_FLASH_MS = 400;
static const uint32_t FRAME_MS = 100;

Arduino_DataBus *bus = new Arduino_ESP32PAR8Q(PIN_LCD_DC, PIN_LCD_CS, PIN_LCD_WR, PIN_LCD_RD,
                                              39, 40, 41, 42, 45, 46, 47, 48);
Arduino_GFX *display = new Arduino_ST7789(bus, PIN_LCD_RST, 1 /* landscape */, true /* IPS */, 170, 320,
                                          35, 0, 35, 0);
// Frames are drawn off-screen and pushed in one go, so clearing never shows as a flash.
Arduino_Canvas *gfx = new Arduino_Canvas(320, 170, display);

static Preferences prefs;
static int themeIndex = 0;
static bool flipped = false;

// The Arduino core frees the BLE controller's memory at boot unless a BLE library claims it;
// NimBLE-Arduino doesn't, so claim it here or controller init fails.
extern "C" bool bleInUse(void) {
    return true;
}

struct Latest {
    bool valid = false;
    char name[16] = "";
    int rssi = 0;
    BthomeReading reading;
    uint32_t receivedAt = 0;
    uint32_t seq = 0;  // bumps on every accepted packet
};

static Latest latest;
static portMUX_TYPE latestLock = portMUX_INITIALIZER_UNLOCKED;

class ScanCallbacks : public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice *dev) override {
        if (!dev->haveName() || dev->getName().rfind(NAME_PREFIX, 0) != 0) {
            return;
        }
        for (int i = 0; i < dev->getServiceDataCount(); i++) {
            if (!(dev->getServiceDataUUID(i) == NimBLEUUID(BTHOME_UUID))) {
                continue;
            }
            std::string sd = dev->getServiceData(i);
            BthomeReading r;
            if (!bthomeParse((const uint8_t *)sd.data(), sd.size(), r) || !r.hasCo2) {
                return;
            }
            portENTER_CRITICAL(&latestLock);
            bool duplicate = latest.valid && r.hasPacketId && latest.reading.hasPacketId &&
                             latest.reading.packetId == r.packetId && strcmp(latest.name, dev->getName().c_str()) == 0;
            if (!duplicate) {
                latest.valid = true;
                strlcpy(latest.name, dev->getName().c_str(), sizeof(latest.name));
                latest.rssi = dev->getRSSI();
                latest.reading = r;
                latest.receivedAt = millis();
                latest.seq++;
            }
            portEXIT_CRITICAL(&latestLock);
            return;
        }
    }
};

struct Button {
    int pin;
    bool wasDown;
    uint32_t changedAt;
};

// Debounce state is per physical button; roles swap when the display is flipped (see pollButtons).
static Button button14 = {PIN_BUTTON_THEME, false, 0};
static Button button0 = {PIN_BUTTON_ROTATE, false, 0};
static bool prefsDirty = false;

// True once per press (active-low, debounced).
static bool pressed(Button &b) {
    bool down = digitalRead(b.pin) == LOW;
    if (down == b.wasDown || millis() - b.changedAt < 30) {
        return false;
    }
    b.changedAt = millis();
    b.wasDown = down;
    return down;
}

static void applyRotation() {
    display->setRotation(flipped ? ROTATION_FLIPPED : ROTATION_NORMAL);
}

// Returns true if a button changed what's on screen. Roles follow screen position: when the
// display is flipped the two physical buttons swap, so each action stays on the same side.
[[maybe_unused]] static bool pollButtons() {  // unused in DIGIT_PREVIEW builds
    bool press14 = pressed(button14);
    bool press0 = pressed(button0);
    bool themePress = flipped ? press0 : press14;
    bool rotatePress = flipped ? press14 : press0;

    if (themePress) {
        themeIndex = (themeIndex + 1) % uiThemeCount();
        uiSetTheme(themeIndex);
        Serial.printf("Theme: %s\n", uiThemeName(themeIndex));
    }
    if (rotatePress) {
        flipped = !flipped;
        applyRotation();
        Serial.printf("Display %s\n", flipped ? "flipped" : "normal");
    }
    prefsDirty |= themePress || rotatePress;
    return themePress || rotatePress;
}

#ifdef DIGIT_PREVIEW
// Glyph preview: GPIO14 steps the digit up, GPIO0 steps it down; 10 = dashes.
static int previewDigit = 0;

static bool pollPreviewButtons() {
    bool up = pressed(button14);
    bool down = pressed(button0);
    if (up) {
        previewDigit = (previewDigit + 1) % 11;
    }
    if (down) {
        previewDigit = (previewDigit + 10) % 11;
    }
    if (up || down) {
        Serial.printf("Preview digit: %d\n", previewDigit);
    }
    return up || down;
}
#endif

// Flash writes take a while, so they happen after the screen has already responded.
static void savePrefsIfDirty() {
    if (prefsDirty) {
        prefs.putInt("theme", themeIndex);
        prefs.putBool("flipped", flipped);
        prefsDirty = false;
    }
}

void setup() {
    pinMode(PIN_LCD_POWER, OUTPUT);
    digitalWrite(PIN_LCD_POWER, HIGH);
    pinMode(PIN_LCD_BL, OUTPUT);
    digitalWrite(PIN_LCD_BL, HIGH);
    pinMode(PIN_BUTTON_THEME, INPUT_PULLUP);
    pinMode(PIN_BUTTON_ROTATE, INPUT_PULLUP);

    Serial.begin(115200);
    gfx->begin();  // also initialises the panel behind the canvas
    uiInit(gfx);
    prefs.begin("ui", false);
    themeIndex = prefs.getInt("theme", 0);
    uiSetTheme(themeIndex);
    flipped = prefs.getBool("flipped", false);
    applyRotation();

    NimBLEDevice::init("");
    NimBLEScan *scan = NimBLEDevice::getScan();
    scan->setAdvertisedDeviceCallbacks(new ScanCallbacks(), true /* want duplicates */);
    scan->setActiveScan(false);  // BTHome data is in the advertisement itself
    scan->setInterval(100);
    scan->setWindow(99);
    scan->start(0, nullptr, false);  // scan forever
    Serial.println("Scanning for BTHome advertisements from QP-xxxx...");
}

void loop() {
    static uint32_t loggedSeq = 0;
    static uint32_t lastFrame = 0;

#ifdef DIGIT_PREVIEW
    bool buttonChanged = pollPreviewButtons();
#else
    bool buttonChanged = pollButtons();
#endif
    if (!buttonChanged && millis() - lastFrame < FRAME_MS) {
        savePrefsIfDirty();
        delay(5);
        return;
    }
    lastFrame = millis();

    Latest snapshot;
    portENTER_CRITICAL(&latestLock);
    snapshot = latest;
    portEXIT_CRITICAL(&latestLock);

    uint32_t age = millis() - snapshot.receivedAt;
    if (snapshot.valid && snapshot.seq != loggedSeq) {
        Serial.printf("%s rssi %d: CO2 %u ppm, %.1f C, %.1f %%RH\n", snapshot.name, snapshot.rssi,
                      snapshot.reading.co2, snapshot.reading.tempC, snapshot.reading.humidity);
        loggedSeq = snapshot.seq;
    }

    UiState ui;
    ui.hasData = snapshot.valid;
    ui.stale = snapshot.valid && age > STALE_MS;
    ui.rxFlash = snapshot.valid && age < RX_FLASH_MS;
    ui.name = snapshot.name;
    ui.rssi = snapshot.rssi;
    ui.ageS = age / 1000;
    ui.reading = snapshot.reading;
#ifdef DIGIT_PREVIEW
    ui.hasData = true;
    ui.stale = false;
    ui.name = "PREVIEW";
    ui.rssi = -55;
    ui.ageS = 0;
    ui.previewDigit = previewDigit;
    ui.reading = BthomeReading();
    ui.reading.hasCo2 = true;
    ui.reading.co2 = previewDigit < 10 ? previewDigit * 1111 : 0;
    ui.reading.hasTemp = true;
    ui.reading.tempC = 21.5f;
    ui.reading.hasHumidity = true;
    ui.reading.humidity = 45;
#endif
    uiRender(ui);
    gfx->flush();
}
