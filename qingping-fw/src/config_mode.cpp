// Configuration mode: soft AP (plus station if credentials are saved) serving a small web UI
// for saving Wi-Fi credentials and uploading new firmware to the inactive OTA slot.
#include "config_mode.h"

#include <Arduino.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_mac.h>
#include <esp_phy_init.h>

#include "board.h"
#include "led.h"

static const uint32_t CONFIG_HOLD_MS = 2000;
static const uint32_t CONFIG_IDLE_TIMEOUT_MS = 10 * 60 * 1000;
static const char AP_PASSWORD[] = "qingpingsetup";  // WPA2 needs >= 8 chars
static const char MDNS_NAME[] = "qingping";

static WebServer server(80);
static Preferences prefs;
static uint32_t lastActivity = 0;

bool configButtonHeld() {
    pinMode(PIN_BUTTON, INPUT);
    if (digitalRead(PIN_BUTTON) != LOW) {
        return false;
    }
    uint32_t start = millis();
    while (millis() - start < CONFIG_HOLD_MS) {
        if (digitalRead(PIN_BUTTON) != LOW) {
            ledSetColor(0, 0, 0);
            return false;
        }
        bool on = ((millis() - start) / 150) % 2 == 0;
        ledSetColor(on ? 255 : 0, on ? 255 : 0, on ? 255 : 0);
        delay(20);
    }
    return true;
}

static String htmlEscape(const String &s) {
    String out;
    for (char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out += c;
        }
    }
    return out;
}

static void sendPage(const String &message = "") {
    String ssid = prefs.getString("ssid", "");
    String staStatus = WiFi.status() == WL_CONNECTED ? "connected, " + WiFi.localIP().toString() : "not connected";
    String page =
        "<!doctype html><html><head><meta charset=utf-8>"
        "<meta name=viewport content='width=device-width,initial-scale=1'><title>Qingping setup</title>"
        "<style>body{font-family:system-ui,sans-serif;max-width:32em;margin:1em auto;padding:0 1em}"
        "fieldset{margin:1em 0;border-radius:6px}input{display:block;margin:.4em 0;width:100%;box-sizing:border-box}"
        "input[type=submit]{width:auto}.msg{background:#eef;padding:.5em;border-radius:4px}</style></head><body>"
        "<h2>Qingping setup</h2>";
    if (message.length()) {
        page += "<p class=msg>" + htmlEscape(message) + "</p>";
    }
    page += "<p>Firmware " + String(FW_VERSION) + "<br>Home Wi-Fi: " +
            (ssid.length() ? htmlEscape(ssid) + " (" + staStatus + ")" : String("not set")) + "</p>";
    page +=
        "<form method=post action=/wifi><fieldset><legend>Home Wi-Fi</legend>"
        "<input name=ssid placeholder='Network name' value='" + htmlEscape(ssid) + "'>"
        "<input name=pass type=password placeholder='Password (leave blank to keep current)'>"
        "<input type=submit value=Save></fieldset></form>"
        "<form method=post action=/update enctype=multipart/form-data><fieldset><legend>Firmware</legend>"
        "<input type=file name=firmware accept=.bin><input type=submit value=Upload></fieldset></form>"
        "<form method=post action=/reboot><input type=submit value='Reboot to normal mode'></form>"
        "</body></html>";
    server.send(200, "text/html", page);
}

static void connectStation() {
    String ssid = prefs.getString("ssid", "");
    if (ssid.length()) {
        Serial.printf("Joining home Wi-Fi \"%s\"\n", ssid.c_str());
        WiFi.begin(ssid.c_str(), prefs.getString("pass", "").c_str());
    }
}

static void handleWifi() {
    String ssid = server.arg("ssid");
    String pass = server.arg("pass");
    ssid.trim();
    prefs.putString("ssid", ssid);
    if (pass.length() || !ssid.length()) {
        prefs.putString("pass", pass);
    }
    WiFi.disconnect();
    connectStation();
    sendPage(ssid.length() ? "Saved. Joining \"" + ssid + "\"; reload in a few seconds to see the address."
                           : String("Home Wi-Fi cleared."));
}

static void handleUploadDone() {
    if (Update.hasError()) {
        server.send(500, "text/plain", String("Update failed: ") + Update.errorString());
        return;
    }
    server.sendHeader("Connection", "close");
    server.send(200, "text/plain", "Update OK, rebooting into the new firmware.");
    delay(500);
    ESP.restart();
}

static void handleUploadChunk() {
    HTTPUpload &up = server.upload();
    lastActivity = millis();
    if (up.status == UPLOAD_FILE_START) {
        Serial.printf("Firmware upload: %s\n", up.filename.c_str());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
            Update.printError(Serial);
        }
    } else if (up.status == UPLOAD_FILE_WRITE) {
        if (Update.write(up.buf, up.currentSize) != up.currentSize) {
            Update.printError(Serial);
        }
        bool on = (millis() / 100) % 2 == 0;
        ledSetColor(0, on ? 255 : 0, on ? 255 : 0);  // cyan flicker while writing
    } else if (up.status == UPLOAD_FILE_END) {
        if (Update.end(true)) {
            Serial.printf("Firmware upload complete: %u bytes\n", up.totalSize);
        } else {
            Update.printError(Serial);
        }
    } else if (up.status == UPLOAD_FILE_ABORTED) {
        Update.abort();
        Serial.println("Firmware upload aborted");
    }
}

static void logWifiEvent(arduino_event_id_t event, arduino_event_info_t info) {
    switch (event) {
        case ARDUINO_EVENT_WIFI_AP_STACONNECTED: {
            const uint8_t *m = info.wifi_ap_staconnected.mac;
            Serial.printf("AP: client %02x:%02x:%02x:%02x:%02x:%02x joined\n", m[0], m[1], m[2], m[3], m[4], m[5]);
            break;
        }
        case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED: {
            const uint8_t *m = info.wifi_ap_stadisconnected.mac;
            Serial.printf("AP: client %02x:%02x:%02x:%02x:%02x:%02x left (reason %u)\n", m[0], m[1], m[2], m[3],
                          m[4], m[5], info.wifi_ap_stadisconnected.reason);
            break;
        }
        case ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED:
            Serial.printf("AP: assigned %s\n", IPAddress(info.wifi_ap_staipassigned.ip.addr).toString().c_str());
            break;
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            Serial.printf("Home Wi-Fi: disconnected (reason %u)\n", info.wifi_sta_disconnected.reason);
            break;
        default:
            break;
    }
}

void runConfigMode() {
    Serial.println("\n=== Config mode ===");
    prefs.begin("wifi", false);

    // The NVS may still hold RF calibration written by the stock firmware (older IDF); drop it so
    // the PHY does a full calibration and stores fresh data.
    esp_err_t calErr = esp_phy_erase_cal_data_in_nvs();
    Serial.printf("PHY calibration data erased: %s\n", esp_err_to_name(calErr));
    WiFi.onEvent(logWifiEvent);

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);  // WiFi.macAddress() reads zeros before the driver starts
    char apSsid[20];
    snprintf(apSsid, sizeof(apSsid), "QP-Setup-%02X%02X", mac[4], mac[5]);

    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(apSsid, AP_PASSWORD);
    Serial.printf("Access point \"%s\" (password \"%s\"), http://%s/\n", apSsid, AP_PASSWORD,
                  WiFi.softAPIP().toString().c_str());
    connectStation();
    if (MDNS.begin(MDNS_NAME)) {
        MDNS.addService("http", "tcp", 80);
    }

    server.on("/", HTTP_GET, [] {
        lastActivity = millis();
        sendPage();
    });
    server.on("/wifi", HTTP_POST, [] {
        lastActivity = millis();
        handleWifi();
    });
    server.on("/update", HTTP_POST, handleUploadDone, handleUploadChunk);
    server.on("/reboot", HTTP_POST, [] {
        server.send(200, "text/plain", "Rebooting into normal mode.");
        delay(500);
        ESP.restart();
    });
    server.begin();

    lastActivity = millis();
    wl_status_t lastStatus = WL_IDLE_STATUS;
    for (;;) {
        server.handleClient();

        wl_status_t st = WiFi.status();
        if (st != lastStatus) {
            if (st == WL_CONNECTED) {
                Serial.printf("Home Wi-Fi connected: http://%s/ (or http://%s.local/)\n",
                              WiFi.localIP().toString().c_str(), MDNS_NAME);
            }
            lastStatus = st;
        }

        // slow blue breathing = config mode
        int phase = (millis() / 8) % 512;
        int level = phase < 256 ? phase : 511 - phase;
        ledSetColor(0, 0, level);

        if (millis() - lastActivity > CONFIG_IDLE_TIMEOUT_MS) {
            Serial.println("Config mode idle timeout, rebooting");
            ESP.restart();
        }
        delay(2);
    }
}
