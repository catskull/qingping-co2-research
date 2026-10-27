// This unit's factory-MAC eFuse (BLK0) fails its CRC check, and ESP-IDF's PHY init calls
// esp_efuse_mac_get_default() directly, aborting Wi-Fi and BLE startup. The linker flag
// --wrap=esp_efuse_mac_get_default (platformio.ini) sends every caller here instead.
#include <esp_efuse.h>
#include <esp_efuse_table.h>
#include <esp_mac.h>

extern "C" esp_err_t __real_esp_efuse_mac_get_default(uint8_t *mac);

extern "C" esp_err_t __wrap_esp_efuse_mac_get_default(uint8_t *mac) {
    esp_err_t err = __real_esp_efuse_mac_get_default(mac);
    if (err == ESP_ERR_INVALID_CRC) {
        // Only the CRC is wrong; the MAC bytes themselves are the factory-assigned address.
        err = esp_efuse_read_field_blob(ESP_EFUSE_MAC_FACTORY, mac, 48);
    }
    return err;
}
