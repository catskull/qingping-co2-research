// BTHome v2 (https://bthome.io), unencrypted, broadcast-only.
// Service data UUID 0xFCD2; objects must be in ascending object-ID order.
#include "bthome.h"

#include <Arduino.h>
#include <NimBLEDevice.h>

static const uint16_t BTHOME_UUID = 0xFCD2;
static const uint8_t BTHOME_DEVICE_INFO = 0x40;  // v2, unencrypted, regular interval
static const uint8_t OBJ_PACKET_ID = 0x00;       // uint8
static const uint8_t OBJ_TEMPERATURE = 0x02;     // sint16, 0.01 C
static const uint8_t OBJ_HUMIDITY = 0x03;        // uint16, 0.01 %
static const uint8_t OBJ_CO2 = 0x12;             // uint16, 1 ppm

static const uint16_t ADV_INTERVAL = 1600;  // 1 s in 0.625 ms units

// The Arduino core frees the BLE controller's memory at boot unless a BLE library claims it;
// its own BLE library does, NimBLE-Arduino doesn't, so claim it here or controller init fails.
extern "C" bool bleInUse(void) {
    return true;
}

static NimBLEAdvertising *adv = nullptr;
static std::string advName;

void bthomeBegin(const char *name) {
    advName = name;
    NimBLEDevice::init(name);
    adv = NimBLEDevice::getAdvertising();
    adv->setAdvertisementType(BLE_GAP_CONN_MODE_NON);
    adv->setMinInterval(ADV_INTERVAL);
    adv->setMaxInterval(ADV_INTERVAL + 160);
}

static void put16(uint8_t *p, int &n, uint16_t v) {
    p[n++] = v & 0xFF;
    p[n++] = v >> 8;
}

void bthomeUpdate(float tempC, float humidity, uint16_t co2) {
    static uint8_t packetId = 0;
    uint8_t p[16];
    int n = 0;
    p[n++] = BTHOME_DEVICE_INFO;
    p[n++] = OBJ_PACKET_ID;
    p[n++] = packetId++;
    p[n++] = OBJ_TEMPERATURE;
    put16(p, n, (uint16_t)(int16_t)lroundf(tempC * 100));
    p[n++] = OBJ_HUMIDITY;
    put16(p, n, (uint16_t)lroundf(humidity * 100));
    p[n++] = OBJ_CO2;
    put16(p, n, co2);

    NimBLEAdvertisementData data;
    data.setFlags(0x06);  // LE general discoverable, BR/EDR not supported
    data.setName(advName);
    data.setServiceData(NimBLEUUID(BTHOME_UUID), std::string((const char *)p, n));

    adv->stop();
    adv->setAdvertisementData(data);
    adv->start();
}
