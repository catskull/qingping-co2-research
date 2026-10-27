#pragma once

#include <stdint.h>

// Starts non-connectable BLE advertising under the given name (keep it <= 10 chars to fit).
void bthomeBegin(const char *name);
// Replaces the advertised BTHome v2 payload with new readings.
void bthomeUpdate(float tempC, float humidity, uint16_t co2);
