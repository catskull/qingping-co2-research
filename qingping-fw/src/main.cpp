// Custom firmware for the Qingping Air Monitor Lite (ESP32-WROVER-B).
// Normal mode: Wi-Fi off, SCD40 readings broadcast as BTHome over BLE, CO2 shown on the RGB LED.
// Hold the button at power-on for config mode (Wi-Fi setup + firmware upload); see config_mode.cpp.
#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <esp_efuse.h>
#include <esp_efuse_table.h>
#include <esp_mac.h>

#include "board.h"
#include "bthome.h"
#include "config_mode.h"
#include "led.h"
#include "scd40.h"

static bool scdOk = false;

// The factory MAC's bad eFuse CRC is handled by efuse_mac_fix.cpp; also pin the base MAC
// here so esp_read_mac() never falls back to its own eFuse read.
static void fixBaseMac() {
    uint8_t mac[6];
    esp_err_t err = esp_efuse_mac_get_default(mac);
    if (err == ESP_OK) {
        err = esp_base_mac_addr_set(mac);
    }
    Serial.printf("Base MAC %02x:%02x:%02x:%02x:%02x:%02x (%s)\n", mac[0], mac[1], mac[2], mac[3], mac[4],
                  mac[5], esp_err_to_name(err));
}

static void showCo2(uint16_t co2) {
    if (co2 < 800) {
        ledSetColor(0, 255, 0);
    } else if (co2 < 1200) {
        ledSetColor(255, 160, 0);
    } else {
        ledSetColor(255, 0, 0);
    }
}

void setup() {
    pinMode(PIN_POWER_LATCH, OUTPUT);
    digitalWrite(PIN_POWER_LATCH, HIGH);
    pinMode(PIN_PERIPH_EN, OUTPUT);
    digitalWrite(PIN_PERIPH_EN, HIGH);
    ledInit();
    Serial.begin(115200);
    fixBaseMac();

    // Checked before any sensor/BLE code so a buggy build can still be replaced over Wi-Fi.
    if (configButtonHeld()) {
        runConfigMode();
    }

    Serial.printf("\n=== Qingping firmware %s ===\n", FW_VERSION);
    WiFi.mode(WIFI_OFF);

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);  // WiFi.macAddress() reads zeros with Wi-Fi off
    char name[12];
    snprintf(name, sizeof(name), "QP-%02X%02X", mac[4], mac[5]);
    bthomeBegin(name);
    Serial.printf("BTHome name: %s\n", name);

    Wire.begin(PIN_SENSOR_SDA, PIN_SENSOR_SCL, I2C_FREQ);
    scdOk = scd40Begin(Wire);
    ledSetColor(0, 0, scdOk ? 255 : 0);  // blue until the first reading
    if (!scdOk) {
        ledSetColor(255, 0, 255);  // magenta = sensor error
    }
}

void loop() {
    static uint32_t lastPoll = 0;
    if (scdOk && millis() - lastPoll >= 1000) {
        lastPoll = millis();
        Scd40Reading r;
        if (scd40Poll(r)) {
            Serial.printf("CO2 %u ppm  |  %.1f C  |  %.1f %%RH\n", r.co2, r.tempC, r.humidity);
            showCo2(r.co2);
            bthomeUpdate(r.tempC, r.humidity, r.co2);
        }
    }
    delay(20);
}
