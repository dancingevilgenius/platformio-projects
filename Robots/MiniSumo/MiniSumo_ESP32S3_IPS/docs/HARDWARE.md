# MiniSumo_ESP32S3_IPS — Hardware Reference

Agent-facing hardware notes for the ES3C28P-based mini-sumo **controller** (start/stop/diagnostics). Prefer this file over guessing pinouts.

| | |
| --- | --- |
| **Board** | Hosyond / LCDWiki **ES3C28P** |
| **Sibling SKU (no touch)** | ES3N28P |
| **Vendor page** | https://lcdwiki.com/2.8inch_ESP32-S3_Display |
| **Specification PDF** | https://www.lcdwiki.com/res/ES3C28P/ES3C28P_ES2N28P_Specification_V1.0.pdf |
| **User manual PDF** | https://www.lcdwiki.com/res/ES3C28P/2.8inch_IPS_ESP32-S3_ES3C28P_ES3N28P_User_Manual.pdf |
| **Firmware pin map** | [`include/BoardConfig.h`](../include/BoardConfig.h) |

This board has **no on-board motor driver**. Sensors and motors attach via expansion GPIOs / I2C / UART.

---

## Capability matrix (sumo-relevant)

| Capability | On board? | Notes |
| --- | --- | --- |
| Display (UI) | Yes | 2.8" IPS 240×320 ILI9341V, SPI |
| Capacitive touch | Yes (ES3C28P) | FT6336G @ I2C `0x38` |
| Speaker / sound out | Yes | External mono speaker; ES8311 + FM8002E |
| Microphone | Yes | On-board MEMS → ES8311 |
| Wi‑Fi / BLE | Yes | ESP32-S3 |
| USB | Yes | Type‑C CDC / download |
| MicroSD | Yes | SDIO |
| Battery | Yes | 3.7 V LiPo + TP4054 + ADC on GPIO9 |
| RGB status LED | Yes | GPIO42 |
| Free GPIOs | Limited | Expansion: **GPIO2, 3, 14, 21**; UART0 TX/RX if unused |
| External I2C | Yes | Shared with touch + codec (GPIO15/16) |
| Motor controller | **No** | External driver required |
| Opponent / edge sensors | **No** | Add next on expansion / I2C |

---

## MCU / memory

| Item | Value |
| --- | --- |
| SoC | ESP32-S3 (N16R8) |
| PSRAM | 8 MB OPI |
| Flash | 16 MB QSPI |
| Wi‑Fi | 2.4 GHz 802.11b/g/n |
| Bluetooth | v5.0 BR/EDR + BLE |

---

## Display

| Item | Value |
| --- | --- |
| Size / type | 2.8" IPS TFT |
| Resolution | 240 × 320 |
| Driver | ILI9341V |
| Interface | 4-wire SPI |

### LCD pins

| Signal | GPIO | Notes |
| --- | --- | --- |
| CS | 10 | Active low |
| DC | 46 | High = data, low = command |
| SCK | 12 | |
| MOSI | 11 | |
| MISO | 13 | |
| RST | EN / CHIP_PU | Shared with ESP32 reset — **do not** drive a separate LCD reset GPIO |
| BL | 45 | PWM brightness |

**Firmware note:** This IPS panel needs RGB565 channel inversion; see `invert565` / `panelColor` in `BoardConfig.h`.

---

## Capacitive touch

| Item | Value |
| --- | --- |
| Controller | FT6336G |
| Address | `0x38` |
| SDA / SCL | GPIO16 / GPIO15 (shared) |
| RST / INT | GPIO18 / GPIO17 |

---

## Audio (optional later)

| Signal | GPIO |
| --- | --- |
| Amp enable (active low) | 1 |
| I2S MCLK / BCLK / DOUT / LRCK / DIN | 4 / 5 / 6 / 7 / 8 |
| Codec I2C | same as touch; address `0x18` |

---

## Expansion I/O (sensors / motor driver)

Free pins on **1.25 mm 4P** header:

| GPIO | Suggested uses |
| --- | --- |
| **2** | Edge / opponent sensor, PWM, SPI |
| **3** | Edge / opponent sensor, PWM, SPI |
| **14** | Motor enable / PWM / SPI |
| **21** | Motor enable / PWM / SPI |

### External I2C header

| Signal | GPIO | Notes |
| --- | --- |
| SDA | 16 | Same bus as touch + ES8311 |
| SCL | 15 | Same bus as touch + ES8311 |

Keep I2C addresses unique. Avoid long bus blocks if touch UX matters.

### UART header

| Signal | GPIO |
| --- | --- |
| TX0 | 44 |
| RX0 | 43 |

### Battery sense

| Signal | GPIO | Notes |
| --- | --- | --- |
| Pack voltage | **9** | ADC via ÷2 divider → multiply reading ×2 |

### RGB LED

| Signal | GPIO |
| --- | --- |
| Data | **42** |

---

## Full GPIO allocation summary

| GPIO | Function |
| --- | --- |
| 0 | BOOT button |
| 1 | Audio amp enable (active low) |
| 2–3, 14, 21 | Expansion I/O |
| 4–8 | I2S audio |
| 9 | Battery ADC |
| 10–13, 45–46 | LCD SPI + backlight + DC |
| 15–18 | Touch I2C + INT/RST |
| 19–20 | USB D− / D+ |
| 38–41, 47–48 | MicroSD SDIO |
| 42 | RGB LED |
| 43–44 | UART0 |
| 26–37 | Internal PSRAM / Flash — do not use |
| EN | Reset (ESP32 + LCD) |

---

## Firmware roadmap (this repo)

| Phase | Status |
| --- | --- |
| Controller UI (start / stop / diagnostics) | In progress |
| Sensors on expansion / I2C | Planned |
| External motor control | Planned |

Unused today but present on PCB: mic capture, MicroSD, audio playback, RGB LED, UART header beyond USB CDC.
