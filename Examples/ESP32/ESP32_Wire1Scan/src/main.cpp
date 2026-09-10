#include <Arduino.h>
#include <Wire.h>

// ESP32-C3 has only I2C0, and Wire1 has no default pins on that chip.
// SparkFun Pro Micro ESP32-C3 Qwiic / labeled SDA,SCL are GPIO 5 and 6.
// platformio.ini uses esp32-c3-devkitm-1, whose SDA/SCL macros are 8/9.
#if CONFIG_IDF_TARGET_ESP32C3
#define I2C_PORT Wire
static const int I2C_SDA_PIN = 5;
static const int I2C_SCL_PIN = 6;
#else
#define I2C_PORT Wire1
#endif

void setup() {
  Serial.begin(115200);
  // Do not wait for Serial: PlatformIO monitor_dtr=0, so while (!Serial)
  // never becomes true on this C3 USB-Serial/JTAG port.
  delay(1000);

#if CONFIG_IDF_TARGET_ESP32C3
  I2C_PORT.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Serial.printf("I2C scanner starting on SDA=%d SCL=%d\n", I2C_SDA_PIN, I2C_SCL_PIN);
#else
  I2C_PORT.begin();
  Serial.println("I2C scanner starting on Wire1");
#endif
}

void loop() {
  byte error, address;
  int nDevices = 0;

  delay(5000);

  Serial.println("Scanning for I2C devices ...");
  for (address = 0x01; address < 0x7f; address++) {
    I2C_PORT.beginTransmission(address);
    error = I2C_PORT.endTransmission();
    if (error == 0) {
      Serial.printf("I2C device found at address 0x%02X\n", address);
      nDevices++;
    } else if (error != 2) {
      Serial.printf("Error %u at address 0x%02X\n", error, address);
    }
  }
  if (nDevices == 0) {
    Serial.println("No I2C devices found");
  }
}

