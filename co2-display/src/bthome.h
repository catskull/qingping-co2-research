// Minimal BTHome v2 decoder (https://bthome.io) for unencrypted advertisements.
#pragma once

#include <stddef.h>
#include <stdint.h>

static const uint16_t BTHOME_UUID = 0xFCD2;

struct BthomeReading {
    bool hasPacketId = false;
    uint8_t packetId = 0;
    bool hasTemp = false;
    float tempC = 0;
    bool hasHumidity = false;
    float humidity = 0;
    bool hasCo2 = false;
    uint16_t co2 = 0;
};

// Parses BTHome service data (the bytes after the 0xFCD2 UUID). Returns false for encrypted or
// non-v2 payloads. Stops at the first unknown object ID, keeping what was decoded so far.
bool bthomeParse(const uint8_t *data, size_t len, BthomeReading &out);
