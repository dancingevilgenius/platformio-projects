#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

// Maker-ESP32 Pro: four WS2812 LEDs on GPIO 16.
// Same wiring as NULLLAB rgbTest.zip.
#define RGB_PIN 16
#define NUM_LEDS 4
#define BRIGHTNESS 20

Adafruit_NeoPixel rgb(NUM_LEDS, RGB_PIN, NEO_GRB + NEO_KHZ800);

static void showAll(uint8_t r, uint8_t g, uint8_t b, uint16_t holdMs) {
  for (int i = 0; i < NUM_LEDS; i++) {
    rgb.setPixelColor(i, rgb.Color(r, g, b));
  }
  rgb.show();
  delay(holdMs);
}

static void chase(uint8_t r, uint8_t g, uint8_t b, uint16_t holdMs) {
  for (int i = 0; i < NUM_LEDS; i++) {
    rgb.clear();
    rgb.setPixelColor(i, rgb.Color(r, g, b));
    rgb.show();
    delay(holdMs);
  }
}

static void rainbowCycle(uint16_t waitMs) {
  for (int firstHue = 0; firstHue < 65536; firstHue += 256) {
    rgb.rainbow(firstHue);
    rgb.show();
    delay(waitMs);
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("Maker-ESP32 Pro RGB demo (GPIO 16, 4 LEDs)");

  rgb.begin();
  rgb.setBrightness(BRIGHTNESS);
  rgb.clear();
  rgb.show();
}

void loop() {
  // Matches official rgbTest: all four blue, red, then green.
  Serial.println("All blue / red / green");
  showAll(0, 0, 255, 1000);
  showAll(255, 0, 0, 1000);
  showAll(0, 255, 0, 1000);

  Serial.println("Chase around RGB1..RGB4");
  chase(255, 255, 255, 200);
  chase(255, 80, 0, 200);

  Serial.println("Rainbow");
  rainbowCycle(8);

  rgb.clear();
  rgb.show();
  delay(400);
}
