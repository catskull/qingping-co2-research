// Pin map for the Qingping Air Monitor Lite (ESP32-WROVER-B). See esp32-dump/PINOUT.md.
#pragma once

static const char FW_VERSION[] = "0.1.3";

static const int PIN_POWER_LATCH = 23;  // must stay high or the board may cut its own power on battery
static const int PIN_PERIPH_EN = 13;    // powers sensors / touch controller
static const int PIN_BUTTON = 36;       // active-low, external pull-up

static const int PIN_SENSOR_SDA = 26;   // I2C bus 0: SCD40
static const int PIN_SENSOR_SCL = 27;
static const uint32_t I2C_FREQ = 100000;

static const int PIN_LED_RED = 25;      // RGB LED, active-high PWM
static const int PIN_LED_GREEN = 21;
static const int PIN_LED_BLUE = 19;
