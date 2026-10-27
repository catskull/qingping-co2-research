# Qingping Air Monitor Lite (CGDN1 / AirMX variant) — ESP32-WROVER-B pinout

Recovered by static analysis of `app_ota0.bin` (Sparrow_v1.0.2-264-g6c0ce49).
Confidence: **confirmed** = constant pin passed directly to a driver call with clear context;
**likely** = pin confirmed, function inferred from surrounding code/logs.

| GPIO | Dir | Function | Confidence |
|---|---|---|---|
| 26 | I2C0 SDA | Sensor bus (config table value 20000 — likely 20 kHz) | confirmed |
| 27 | I2C0 SCL | Sensor bus | confirmed |
| 12 | I2C1 SDA | Touch controller bus, 400 kHz | confirmed |
| 14 | I2C1 SCL | Touch controller bus | confirmed |
| 32 | in, pull-up | Touch controller (ATtiny814) interrupt, active-low while a finger is down | confirmed on hardware |
| 4  | SPI2 MOSI | OLED data | confirmed |
| 5  | SPI2 SCLK | OLED clock (2 MHz, or 8 MHz for lcd id 1) | confirmed |
| 15 | out | OLED CS (driven manually, low = selected) | confirmed |
| 2  | out | OLED D/C (1 = data, 0 = command) | confirmed |
| 22 | out | OLED RST (pulsed 1→0→1 at init) | confirmed |
| 25 | PWM (LEDC) | RGB LED — red | confirmed on hardware |
| 21 | PWM (LEDC) | RGB LED — green | confirmed on hardware |
| 19 | PWM (LEDC) | RGB LED — blue | confirmed on hardware |
| 36 | in, ISR | Power/display button, active-low (0 = pressed; input-only pin, external pull-up) | confirmed on hardware |
| 23 | out | Power latch — high at boot, low on "power off" | likely |
| 13 | out | Peripheral power enable (set high after ADC init) | likely |
| 18 | out | Battery-measurement enable (set high in ADC init) | likely |
| 39 | ADC1_CH3, 11 dB | Battery voltage | likely |
| 34 | in | USB/VBUS present (gates all charging logic) | likely |
| 35 | in + ADC1_CH7 | Charger status (read only while on USB) | likely |
| 1 / 3 | UART0 | Console / flashing | confirmed |

## I2C devices

| Bus | 7-bit addr | Device |
|---|---|---|
| 0 (27/26) | 0x62 | Sensirion SCD40 — CO2 (+T/RH) |
| 0 (27/26) | 0x44 | Temp/humidity sensor (Sensirion SHT3x/SHT4x address) |
| 0 (27/26) | 0x12 | PM2.5/PM10 sensor (address matches Plantower PMSA003I) |
| 1 (14/12) | 0x0C | ATtiny814 touch controller |

Hardware scan (GPIO13 high; same result at 100 kHz and 20 kHz, GPIO18 low or high):
- bus 0: **0x10** and **0x62** (SCD40 confirmed, serial 1F430F073B9E). Nothing at 0x44 or 0x12
  (PM2.5 sensor removed from this unit).
- bus 1: **0x0C** (touch controller confirmed). It did not answer a scan made ~1 s after GPIO13 went
  high, but answered once the board had been up ~1.5 s — give it boot time.
- 0x10 is never addressed by the stock firmware; identity unknown. The stock temp/RH sensor (0x44) is
  not answering, so 0x10 may be a substitute T/RH part on this hardware revision.
- Idle levels (powered from the device's own USB-C, battery unplugged, no touch): GPIO32 = 1,
  GPIO34 = 0, GPIO35 = 0. GPIO34 reading 0 *with* USB-C power contradicts the "USB present, active-high"
  guess — it is either active-low or something else; GPIO34/35 roles are unconfirmed.

## Touch slider (ATtiny814, bus 1, 0x0C)

Plain 2-byte I2C read, no register select, returning a position byte and a flags byte:
- position = finger position along the strip (observed ~9 to ~243 end to end)
- flags = bit 7 set while a finger is down, 0 after release. Bits 2–3 give the direction of motion:
  0x80 = still, 0x84 = moving toward lower positions, 0x88 = moving toward higher positions
  (confirmed on hardware)
- Nominal order is position then flags (matches the stock firmware), but the controller streams the
  two bytes round-robin and keeps its place across aborted reads, so the order flips after a failed
  read. Identify the flags byte by `(b & 0xF3) == 0x80` instead of trusting the order.

Reads often NACK when the controller isn't asserting INT; poll only while GPIO32 is low.
Allow ~1.5 s after GPIO13 goes high before the first read. Stock firmware classifies a touch as a
swipe if it travels more than 37 position units, and a tap if less than 25.

## OLED

SPI, write-only, manual CS. Init sequence uses `0xAD` (DC-DC control) and `0xA8 0x7F` (128-row mux):
an SH1107-class controller. Page writes are 0x50 (80) bytes × 16 pages.

## Gotchas for custom firmware

- **Factory MAC eFuse fails its CRC** on this unit (`efuse_crc = 0x40; calc_crc = 0x61`), and esptool
  reports the chip as "Unknown ESP32". Stock Arduino/IDF Wi-Fi and BLE init abort with
  `ESP_ERR_INVALID_CRC`. Call `esp_base_mac_addr_set()` with the custom MAC (BLK3) or the raw factory
  MAC (read with `esp_efuse_read_field_blob(ESP_EFUSE_MAC_FACTORY, ...)`) before
  any radio init.
- **NimBLE-Arduino on Arduino-ESP32 3.x** needs a strong `extern "C" bool bleInUse() { return true; }`,
  or the core frees BLE controller memory at boot and `esp_bt_controller_init` fails.

- **GPIO12 is a strapping pin** (flash voltage). It has an I2C pull-up here; the board only boots because
  the VDD_SDIO eFuse forces 3.3 V (`Flash voltage set by eFuse: 3.3V`). Don't change that eFuse.
- GPIO2 and GPIO15 are also strapping pins (used for OLED D/C and CS) — fine as outputs after boot.
- GPIO16/17 are taken by PSRAM on the WROVER module.
- **Drive GPIO23 high early** in your firmware, or the device may cut its own power when running on battery.
- An unused UART1 path (TX 18 / RX 19) exists in the Xiaomi `arch_serial` library; it conflicts with the
  LED/enable pins and is not used on this board.
