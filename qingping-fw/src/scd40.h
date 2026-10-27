#pragma once

#include <Wire.h>

// Sensor default is 4.0 C; +1.4 C (2.5 F) more measured against a reference thermometer overnight.
static const float SCD40_TEMP_OFFSET_C = 5.4f;

struct Scd40Reading {
    uint16_t co2;  // ppm
    float tempC;
    float humidity;  // %RH
};

// Stops any running measurement, logs the serial number, and starts periodic measurement (5 s).
bool scd40Begin(TwoWire &bus);
// Returns true and fills `out` only when a new measurement is available.
bool scd40Poll(Scd40Reading &out);
