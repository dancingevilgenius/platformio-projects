#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include "BoardConfig.h"

class BoardDisplay : public lgfx::LGFX_Device {
 public:
  BoardDisplay() {
    {
      auto cfg = _bus_instance.config();
      cfg.spi_host   = SPI2_HOST;
      cfg.spi_mode   = 0;
      cfg.freq_write = 40000000;
      cfg.freq_read  = 16000000;
      cfg.spi_3wire  = false;
      cfg.use_lock   = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk   = PIN_LCD_SCK;
      cfg.pin_mosi   = PIN_LCD_MOSI;
      cfg.pin_miso   = PIN_LCD_MISO;
      cfg.pin_dc     = PIN_LCD_DC;
      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }

    {
      auto cfg = _panel_instance.config();
      cfg.pin_cs           = PIN_LCD_CS;
      cfg.pin_rst          = -1;  // shared with ESP reset
      cfg.pin_busy         = -1;
      cfg.memory_width     = LCD_WIDTH;
      cfg.memory_height    = LCD_HEIGHT;
      cfg.panel_width      = LCD_WIDTH;
      cfg.panel_height     = LCD_HEIGHT;
      cfg.offset_x         = 0;
      cfg.offset_y         = 0;
      cfg.offset_rotation  = 0;
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits  = 1;
      cfg.readable         = true;
      cfg.invert           = false;
      cfg.rgb_order        = false;
      cfg.dlen_16bit       = false;
      cfg.bus_shared       = true;
      _panel_instance.config(cfg);
    }

    {
      auto cfg = _light_instance.config();
      cfg.pin_bl       = PIN_LCD_BL;
      cfg.invert       = false;
      cfg.freq         = 44100;
      cfg.pwm_channel  = 7;
      _light_instance.config(cfg);
      _panel_instance.setLight(&_light_instance);
    }

    {
      auto cfg = _touch_instance.config();
      cfg.x_min      = 0;
      cfg.x_max      = LCD_WIDTH - 1;
      cfg.y_min      = 0;
      cfg.y_max      = LCD_HEIGHT - 1;
      // Do not gate on INT. LovyanGFX writes FT6336 INTMODE=poll (0xA4=0),
      // and in that mode GPIO17 often stays high, so getTouch() always
      // returns 0 even while a finger is down.
      cfg.pin_int    = -1;
      cfg.pin_rst    = PIN_TOUCH_RST;
      cfg.bus_shared = false;
      // Overlay is 180° from the panel: invert X and Y (left/right and
      // top/bottom). That is why tapping "MINI SUMO" opened Calibrate.
      cfg.offset_rotation = 2;
      cfg.i2c_port   = TOUCH_I2C_PORT;
      cfg.i2c_addr   = TOUCH_I2C_ADDR;
      cfg.pin_sda    = PIN_TOUCH_SDA;
      cfg.pin_scl    = PIN_TOUCH_SCL;
      cfg.freq       = TOUCH_I2C_FREQ;
      _touch_instance.config(cfg);
      _panel_instance.setTouch(&_touch_instance);
    }

    setPanel(&_panel_instance);
  }

  void setBacklight(uint8_t level) { _panel_instance.setBrightness(level); }

  void panelSleep() { _panel_instance.setSleep(true); }
  void panelWake() { _panel_instance.setSleep(false); }

  bool setTouchPowerMode(uint8_t mode) {
    return lgfx::i2c::writeRegister8(TOUCH_I2C_PORT, TOUCH_I2C_ADDR,
                                     FT6336_PMODE_REG, mode, 0, TOUCH_I2C_FREQ)
        .has_value();
  }

  // LovyanGFX getTouch, then a raw FT6336 read if the driver rejected init
  // (some chips report vendor id 0, which fails Touch_FT5x06::_check_init).
  bool readTouch(int16_t* x, int16_t* y) {
    lgfx::touch_point_t tp{};
    if (getTouch(&tp) > 0) {
      *x = tp.x;
      *y = tp.y;
      return true;
    }

    uint8_t buf[5] = {};
    const uint8_t reg = 0x02;
    if (!lgfx::i2c::transactionWriteRead(TOUCH_I2C_PORT, TOUCH_I2C_ADDR, &reg, 1,
                                         buf, sizeof(buf), TOUCH_I2C_FREQ)
             .has_value()) {
      return false;
    }
    if ((buf[0] & 0x0F) == 0) return false;

    tp.x = static_cast<int16_t>(((buf[1] & 0x0F) << 8) | buf[2]);
    tp.y = static_cast<int16_t>(((buf[3] & 0x0F) << 8) | buf[4]);
    convertRawXY(&tp);
    *x = tp.x;
    *y = tp.y;
    return true;
  }

 private:
  lgfx::Panel_ILI9341 _panel_instance;
  lgfx::Bus_SPI       _bus_instance;
  lgfx::Light_PWM     _light_instance;
  lgfx::Touch_FT5x06  _touch_instance;  // FT6336G-compatible
};
