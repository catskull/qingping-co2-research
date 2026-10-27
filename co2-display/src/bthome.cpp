#include "bthome.h"

// Payload size in bytes for the BTHome v2 object IDs we may encounter; 0 = unknown.
static size_t objectSize(uint8_t id) {
    switch (id) {
        case 0x00: return 1;  // packet id
        case 0x01: return 1;  // battery %
        case 0x02: return 2;  // temperature 0.01 C
        case 0x03: return 2;  // humidity 0.01 %
        case 0x04: return 3;  // pressure
        case 0x05: return 3;  // illuminance
        case 0x08: return 2;  // dewpoint
        case 0x09: return 1;  // count
        case 0x0C: return 2;  // voltage
        case 0x0D: return 2;  // pm2.5
        case 0x0E: return 2;  // pm10
        case 0x0F: return 1;  // generic boolean
        case 0x10: return 1;  // power
        case 0x11: return 1;  // opening
        case 0x12: return 2;  // co2
        case 0x13: return 2;  // tvoc
        case 0x14: return 2;  // moisture
        case 0x2E: return 1;  // humidity 1 %
        case 0x2F: return 1;  // moisture 1 %
        case 0x3A: return 1;  // button event
        case 0x45: return 2;  // temperature 0.1 C
        default: return 0;
    }
}

static uint16_t u16(const uint8_t *p) {
    return p[0] | (p[1] << 8);
}

bool bthomeParse(const uint8_t *data, size_t len, BthomeReading &out) {
    if (len < 1) {
        return false;
    }
    uint8_t info = data[0];
    bool encrypted = info & 0x01;
    uint8_t version = info >> 5;
    if (encrypted || version != 2) {
        return false;
    }

    size_t i = 1;
    while (i < len) {
        uint8_t id = data[i++];
        size_t size = objectSize(id);
        if (size == 0 || i + size > len) {
            break;
        }
        const uint8_t *v = data + i;
        switch (id) {
            case 0x00:
                out.hasPacketId = true;
                out.packetId = v[0];
                break;
            case 0x02:
                out.hasTemp = true;
                out.tempC = (int16_t)u16(v) * 0.01f;
                break;
            case 0x45:
                out.hasTemp = true;
                out.tempC = (int16_t)u16(v) * 0.1f;
                break;
            case 0x03:
                out.hasHumidity = true;
                out.humidity = u16(v) * 0.01f;
                break;
            case 0x2E:
                out.hasHumidity = true;
                out.humidity = v[0];
                break;
            case 0x12:
                out.hasCo2 = true;
                out.co2 = u16(v);
                break;
            default:
                break;
        }
        i += size;
    }
    return true;
}
