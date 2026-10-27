#include "led.h"

#include <Arduino.h>

#include "board.h"

static const int LED_PINS[] = {PIN_LED_RED, PIN_LED_GREEN, PIN_LED_BLUE};
static const int PWM_FREQ = 5000;
static const int PWM_BITS = 8;
static const int LED_BRIGHTNESS = 40;  // out of 255; the LED is very bright at full duty

static void ledWrite(int idx, int duty) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcWrite(LED_PINS[idx], duty);
#else
    ledcWrite(idx, duty);
#endif
}

void ledInit() {
    for (int i = 0; i < 3; i++) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
        ledcAttach(LED_PINS[i], PWM_FREQ, PWM_BITS);
#else
        ledcSetup(i, PWM_FREQ, PWM_BITS);
        ledcAttachPin(LED_PINS[i], i);
#endif
        ledWrite(i, 0);
    }
}

void ledSetColor(int r, int g, int b) {
    ledWrite(0, r * LED_BRIGHTNESS / 255);
    ledWrite(1, g * LED_BRIGHTNESS / 255);
    ledWrite(2, b * LED_BRIGHTNESS / 255);
}
