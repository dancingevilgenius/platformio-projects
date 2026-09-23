#pragma once

#include <stdint.h>

// Hosyond / LCDWiki ES3C28P (ESP32-S3 2.8" IPS capacitive)
// Full board capabilities (audio, mic, SD, battery, expansion I/O, wireless):
// see docs/HARDWARE.md

static constexpr int LCD_WIDTH  = 240;
static constexpr int LCD_HEIGHT = 320;

static constexpr int PIN_LCD_CS  = 10;
static constexpr int PIN_LCD_SCK = 12;
static constexpr int PIN_LCD_MOSI = 11;
static constexpr int PIN_LCD_MISO = 13;
static constexpr int PIN_LCD_DC  = 46;
static constexpr int PIN_LCD_BL  = 45;
// LCD RST is tied to ESP32 EN — do not drive a separate GPIO

static constexpr int PIN_TOUCH_SDA = 16;
static constexpr int PIN_TOUCH_SCL = 15;
static constexpr int PIN_TOUCH_INT = 17;
static constexpr int PIN_TOUCH_RST = 18;

static constexpr int TOUCH_I2C_PORT = 0;
static constexpr uint8_t TOUCH_I2C_ADDR = 0x38;
static constexpr uint32_t TOUCH_I2C_FREQ = 400000;

// FT6336 power mode register. Monitor scans at a reduced rate but still drives
// INT, and the chip promotes itself back to Active on the first contact.
static constexpr uint8_t FT6336_PMODE_REG = 0xA5;
static constexpr uint8_t FT6336_PMODE_ACTIVE = 0x00;
static constexpr uint8_t FT6336_PMODE_MONITOR = 0x01;

// Shared I2C bus (touch + Qwiic accessories + ES8311)
static constexpr int PIN_I2C_SDA = PIN_TOUCH_SDA;
static constexpr int PIN_I2C_SCL = PIN_TOUCH_SCL;

static constexpr int PIN_RGB_LED = 42;

// Audio (ES8311 codec + FM8002E amp). I2C shared with touch / Qwiic.
static constexpr int PIN_AMP_ENABLE = 1;   // FM8002E SHUTDOWN, active low
static constexpr int PIN_I2S_MCLK   = 4;
static constexpr int PIN_I2S_BCLK   = 5;
static constexpr int PIN_I2S_DOUT   = 6;   // ESP32 → codec (playback)
static constexpr int PIN_I2S_LRCK   = 7;
static constexpr int PIN_I2S_DIN    = 8;   // codec → ESP32 (mic)
static constexpr uint8_t ES8311_I2C_ADDR = 0x18;

// Battery sense: GPIO9 ADC via ÷2 divider → multiply reading ×2 for pack V.
static constexpr int PIN_BATTERY_ADC = 9;

// Expansion header (sensors / motor driver later): GPIO 2, 3, 14, 21
static constexpr int PIN_EXP_0 = 2;
static constexpr int PIN_EXP_1 = 3;
static constexpr int PIN_EXP_2 = 14;
static constexpr int PIN_EXP_3 = 21;

// ILI9341 IPS on ES3C28P inverts RGB565 channels; pre-invert logical colors before draw.
static constexpr uint16_t invert565(uint16_t c) {
  return static_cast<uint16_t>(
      (static_cast<uint16_t>(31u - (c >> 11)) << 11) |
      (static_cast<uint16_t>(63u - ((c >> 5) & 0x3Fu)) << 5) |
      static_cast<uint16_t>(31u - (c & 0x1Fu)));
}

static constexpr uint16_t panelColor(uint16_t logical565) {
  return invert565(logical565);
}
