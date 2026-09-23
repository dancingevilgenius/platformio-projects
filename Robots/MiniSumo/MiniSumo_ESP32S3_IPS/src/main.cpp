#include <Arduino.h>

#include "BoardConfig.h"
#include "BoardDisplay.h"
#include "BuildInfo.h"

namespace {

BoardDisplay tft;

enum class Screen : uint8_t { Splash, Home, Diagnostics };
enum class RunState : uint8_t { Idle, Running };

Screen g_screen = Screen::Splash;
RunState g_run = RunState::Idle;
bool g_wasTouching = false;
uint32_t g_diagRefreshMs = 0;
uint32_t g_splashStartMs = 0;
uint8_t g_lastSplashBl = 255;

constexpr uint8_t BL_LEVEL = 200;
constexpr uint32_t SPLASH_HOLD_MS = 4500;
constexpr uint32_t SPLASH_FADE_MS = 500;

// Logical RGB565 → panelColor for this IPS.
constexpr uint16_t COL_BG     = panelColor(0x1082);  // dark slate
constexpr uint16_t COL_PANEL  = panelColor(0x2124);
constexpr uint16_t COL_TEXT   = panelColor(0xFFFF);
constexpr uint16_t COL_MUTED  = panelColor(0x8410);
constexpr uint16_t COL_START  = panelColor(0x07E0);
constexpr uint16_t COL_STOP   = panelColor(0xF800);
constexpr uint16_t COL_ACCENT = panelColor(0x05FF);
constexpr uint16_t COL_IDLE   = panelColor(0xFE60);

struct HitRect {
  int16_t x, y, w, h;
  bool contains(int16_t px, int16_t py) const {
    return px >= x && px < x + w && py >= y && py < y + h;
  }
};

HitRect g_btnStart{16, 120, 100, 56};
HitRect g_btnStop{124, 120, 100, 56};
HitRect g_btnDiag{16, 200, 208, 48};
HitRect g_btnBack{16, 260, 208, 40};

void drawButton(const HitRect& r, uint16_t fill, const char* label,
                uint16_t labelColor = COL_TEXT) {
  tft.fillRoundRect(r.x, r.y, r.w, r.h, 8, fill);
  tft.drawRoundRect(r.x, r.y, r.w, r.h, 8, COL_MUTED);
  tft.setTextDatum(middle_center);
  tft.setTextColor(labelColor);
  tft.setTextSize(2);
  tft.drawString(label, r.x + r.w / 2, r.y + r.h / 2);
}

void drawHome() {
  tft.fillScreen(COL_BG);

  tft.setTextDatum(top_center);
  tft.setTextColor(COL_TEXT);
  tft.setTextSize(2);
  tft.drawString("MINI SUMO", LCD_WIDTH / 2, 16);

  tft.setTextColor(COL_MUTED);
  tft.setTextSize(1);
  tft.drawString("controller", LCD_WIDTH / 2, 44);

  const char* stateLabel = (g_run == RunState::Running) ? "RUNNING" : "IDLE";
  const uint16_t stateColor =
      (g_run == RunState::Running) ? COL_START : COL_IDLE;
  tft.fillRoundRect(40, 68, 160, 36, 6, COL_PANEL);
  tft.setTextDatum(middle_center);
  tft.setTextColor(stateColor);
  tft.setTextSize(2);
  tft.drawString(stateLabel, LCD_WIDTH / 2, 86);

  drawButton(g_btnStart, COL_START, "START", panelColor(0x0000));
  drawButton(g_btnStop, COL_STOP, "STOP", COL_TEXT);
  drawButton(g_btnDiag, COL_PANEL, "DIAGNOSTICS", COL_ACCENT);
}

float readBatteryVolts() {
  // ÷2 divider on GPIO9; ESP32-S3 ADC attenuation ~0–3.3 V at input.
  const int raw = analogRead(PIN_BATTERY_ADC);
  const float adcV = (raw / 4095.0f) * 3.3f;
  return adcV * 2.0f;
}

void drawDiagnostics() {
  tft.fillScreen(COL_BG);

  tft.setTextDatum(top_center);
  tft.setTextColor(COL_TEXT);
  tft.setTextSize(2);
  tft.drawString("DIAGNOSTICS", LCD_WIDTH / 2, 12);

  tft.setTextDatum(top_left);
  tft.setTextSize(1);
  tft.setTextColor(COL_MUTED);

  int y = 48;
  const int line = 16;

  char buf[64];
  snprintf(buf, sizeof(buf), "Battery:  %.2f V", readBatteryVolts());
  tft.setTextColor(COL_TEXT);
  tft.drawString(buf, 16, y);
  y += line;

  snprintf(buf, sizeof(buf), "Free heap: %u B", (unsigned)ESP.getFreeHeap());
  tft.drawString(buf, 16, y);
  y += line;

  snprintf(buf, sizeof(buf), "Uptime:   %lu s",
           (unsigned long)(millis() / 1000UL));
  tft.drawString(buf, 16, y);
  y += line;

  tft.setTextColor(COL_MUTED);
  tft.drawString("Expansion (inputs, later):", 16, y);
  y += line;
  tft.setTextColor(COL_TEXT);
  snprintf(buf, sizeof(buf), "  GPIO2=%d  GPIO3=%d", digitalRead(PIN_EXP_0),
           digitalRead(PIN_EXP_1));
  tft.drawString(buf, 16, y);
  y += line;
  snprintf(buf, sizeof(buf), "  GPIO14=%d GPIO21=%d", digitalRead(PIN_EXP_2),
           digitalRead(PIN_EXP_3));
  tft.drawString(buf, 16, y);
  y += line + 4;

  tft.setTextColor(COL_MUTED);
  tft.drawString("Build:", 16, y);
  y += line;
  tft.setTextColor(COL_TEXT);
  tft.drawString(BUILD_DATE, 16, y);
  y += line;
  // Truncate long commit subjects for the narrow panel.
  String subj(GIT_COMMIT_MSG);
  if (subj.length() > 34) subj = subj.substring(0, 31) + "...";
  tft.drawString(subj.c_str(), 16, y);

  drawButton(g_btnBack, COL_PANEL, "BACK", COL_ACCENT);
}

void drawSplash() {
  tft.fillScreen(COL_BG);
  tft.setTextDatum(middle_center);
  tft.setTextColor(COL_TEXT);
  tft.setTextSize(2);

  const int16_t cx = LCD_WIDTH / 2;
  const int16_t cy = LCD_HEIGHT / 2;
  tft.drawString("What is thy", cx, cy - 28);
  tft.drawString("bidding", cx, cy);
  tft.drawString("my master?", cx, cy + 28);
}

void showScreen(Screen s) {
  g_screen = s;
  if (s == Screen::Splash) {
    g_splashStartMs = millis();
    g_lastSplashBl = 255;
    tft.setBacklight(BL_LEVEL);
    drawSplash();
  } else if (s == Screen::Home) {
    drawHome();
  } else {
    g_diagRefreshMs = 0;
    drawDiagnostics();
  }
}

void fadeSplash() {
  const uint32_t elapsed = millis() - g_splashStartMs;
  if (elapsed < SPLASH_HOLD_MS) {
    return;
  }

  if (elapsed >= SPLASH_HOLD_MS + SPLASH_FADE_MS) {
    tft.setBacklight(0);
    showScreen(Screen::Home);
    tft.setBacklight(BL_LEVEL);
    return;
  }

  const uint32_t t = elapsed - SPLASH_HOLD_MS;
  const uint8_t level =
      static_cast<uint8_t>(BL_LEVEL - (BL_LEVEL * t) / SPLASH_FADE_MS);
  if (level != g_lastSplashBl) {
    g_lastSplashBl = level;
    tft.setBacklight(level);
  }
}

void onStart() {
  if (g_run == RunState::Running) return;
  g_run = RunState::Running;
  Serial.println("[sumo] START");
  // Phase 3: enable motor driver / match timer here.
  drawHome();
}

void onStop() {
  if (g_run == RunState::Idle) return;
  g_run = RunState::Idle;
  Serial.println("[sumo] STOP");
  // Phase 3: disable motors immediately.
  drawHome();
}

void handleTouchPress(int16_t x, int16_t y) {
  if (g_screen == Screen::Home) {
    if (g_btnStart.contains(x, y)) {
      onStart();
    } else if (g_btnStop.contains(x, y)) {
      onStop();
    } else if (g_btnDiag.contains(x, y)) {
      showScreen(Screen::Diagnostics);
    }
  } else if (g_screen == Screen::Diagnostics) {
    if (g_btnBack.contains(x, y)) {
      showScreen(Screen::Home);
    }
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("MiniSumo_ESP32S3_IPS");
  Serial.printf("build %s | %s\n", BUILD_DATE, GIT_COMMIT_MSG);
  Serial.println("What is thy bidding my master?");

  // Expansion pins as inputs with pull-up until sensors are wired.
  pinMode(PIN_EXP_0, INPUT_PULLUP);
  pinMode(PIN_EXP_1, INPUT_PULLUP);
  pinMode(PIN_EXP_2, INPUT_PULLUP);
  pinMode(PIN_EXP_3, INPUT_PULLUP);

  analogReadResolution(12);
  analogSetPinAttenuation(PIN_BATTERY_ADC, ADC_11db);

  tft.init();
  tft.setRotation(0);
  tft.setTouchPowerMode(FT6336_PMODE_ACTIVE);

  showScreen(Screen::Splash);
}

void loop() {
  if (g_screen == Screen::Splash) {
    fadeSplash();
    delay(10);
    return;
  }

  lgfx::touch_point_t tp;
  const bool touching = tft.getTouch(&tp) > 0;

  if (touching && !g_wasTouching) {
    handleTouchPress(tp.x, tp.y);
  }
  g_wasTouching = touching;

  if (g_screen == Screen::Diagnostics) {
    const uint32_t now = millis();
    if (now - g_diagRefreshMs >= 500) {
      g_diagRefreshMs = now;
      drawDiagnostics();
    }
  }

  delay(10);
}
