// Sensirion SCD40: 16-bit commands, 16-bit words each followed by CRC-8.
#include "scd40.h"

#include <Arduino.h>

static const uint8_t SCD40_ADDR = 0x62;
static TwoWire *bus = nullptr;

static uint8_t sensirionCrc(const uint8_t *data, int len) {
    uint8_t crc = 0xFF;
    for (int i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x80) ? (crc << 1) ^ 0x31 : crc << 1;
        }
    }
    return crc;
}

static bool command(uint16_t cmd) {
    bus->beginTransmission(SCD40_ADDR);
    bus->write(cmd >> 8);
    bus->write(cmd & 0xFF);
    return bus->endTransmission() == 0;
}

static bool readWords(uint16_t cmd, uint16_t *words, int count) {
    if (!command(cmd)) {
        return false;
    }
    delay(1);
    int len = count * 3;
    if (bus->requestFrom(SCD40_ADDR, (uint8_t)len) != len) {
        return false;
    }
    for (int i = 0; i < count; i++) {
        uint8_t buf[3];
        for (int j = 0; j < 3; j++) {
            buf[j] = bus->read();
        }
        if (sensirionCrc(buf, 2) != buf[2]) {
            Serial.printf("SCD40 CRC error on word %d\n", i);
            return false;
        }
        words[i] = (buf[0] << 8) | buf[1];
    }
    return true;
}

static bool writeWord(uint16_t cmd, uint16_t word) {
    uint8_t data[2] = {(uint8_t)(word >> 8), (uint8_t)(word & 0xFF)};
    bus->beginTransmission(SCD40_ADDR);
    bus->write(cmd >> 8);
    bus->write(cmd & 0xFF);
    bus->write(data, 2);
    bus->write(sensirionCrc(data, 2));
    return bus->endTransmission() == 0;
}

// Self-heating compensation: the sensor subtracts this from its temperature and recomputes RH
// to match. Not persisted (that would wear the sensor's EEPROM), so it is set on every boot.
static void setTemperatureOffset(float offsetC) {
    uint16_t old;
    if (readWords(0x2318, &old, 1)) {  // get_temperature_offset
        Serial.printf("SCD40 temperature offset: %.2f C -> %.2f C\n", old * 175.0f / 65536.0f, offsetC);
    }
    writeWord(0x241D, (uint16_t)(offsetC * 65536.0f / 175.0f + 0.5f));  // set_temperature_offset
    delay(1);
}

bool scd40Begin(TwoWire &b) {
    bus = &b;
    command(0x3F86);  // stop_periodic_measurement, in case it is already running
    delay(500);
    uint16_t serial[3];
    if (!readWords(0x3682, serial, 3)) {  // get_serial_number
        Serial.println("SCD40: no response");
        return false;
    }
    Serial.printf("SCD40 serial: %04X%04X%04X\n", serial[0], serial[1], serial[2]);
    setTemperatureOffset(SCD40_TEMP_OFFSET_C);
    return command(0x21B1);  // start_periodic_measurement
}

bool scd40Poll(Scd40Reading &out) {
    uint16_t status, w[3];
    if (!readWords(0xE4B8, &status, 1) || (status & 0x07FF) == 0) {  // get_data_ready_status
        return false;
    }
    if (!readWords(0xEC05, w, 3)) {  // read_measurement
        return false;
    }
    out.co2 = w[0];
    out.tempC = -45.0f + 175.0f * w[1] / 65535.0f;
    out.humidity = 100.0f * w[2] / 65535.0f;
    return true;
}
