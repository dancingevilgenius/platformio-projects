#include <Arduino.h>

// Maker-ESP32 Pro: four DC/encoder-motor driver channels.
// Same GPIO map as NULLLAB motorTest.zip (Pro names them M0..M3).
// Original Maker-ESP32 labeled the same pins M1..M4.
#define PWM_FREQ_HZ 5000
#define PWM_BITS 8
#define MOTOR_PWM 255
#define HOLD_MS 1000

struct Motor {
  const char *name;
  int pinA;
  int pinB;
  int chA;
  int chB;
};

static Motor motors[] = {
    {"M0", 27, 13, 0, 1},
    {"M1", 4, 2, 2, 3},
    {"M2", 17, 12, 4, 5},
    {"M3", 14, 15, 6, 7},
};

static void motorWrite(const Motor &m, int dutyA, int dutyB) {
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
  ledcWrite(m.pinA, dutyA);
  ledcWrite(m.pinB, dutyB);
#else
  ledcWrite(m.chA, dutyA);
  ledcWrite(m.chB, dutyB);
#endif
}

static void motorSpeed(const Motor &m, int speed) {
  if (speed > 0) {
    motorWrite(m, speed, 0);
  } else if (speed < 0) {
    motorWrite(m, 0, -speed);
  } else {
    motorWrite(m, 0, 0);
  }
}

static void stopAll() {
  for (size_t i = 0; i < 4; i++) {
    motorSpeed(motors[i], 0);
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("Maker-ESP32 Pro motor demo (M0..M3)");
  Serial.println("Need 6-16V on the DC jack. Set Motor/IO switch to Motor for M2/M3.");

  for (size_t i = 0; i < 4; i++) {
    Motor &m = motors[i];
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
    ledcAttach(m.pinA, PWM_FREQ_HZ, PWM_BITS);
    ledcAttach(m.pinB, PWM_FREQ_HZ, PWM_BITS);
#else
    ledcSetup(m.chA, PWM_FREQ_HZ, PWM_BITS);
    ledcAttachPin(m.pinA, m.chA);
    ledcSetup(m.chB, PWM_FREQ_HZ, PWM_BITS);
    ledcAttachPin(m.pinB, m.chB);
#endif
  }
  stopAll();
}

void loop() {
  for (size_t i = 0; i < 4; i++) {
    Motor &m = motors[i];

    Serial.print(m.name);
    Serial.println(" forward");
    motorSpeed(m, MOTOR_PWM);
    delay(HOLD_MS);

    Serial.print(m.name);
    Serial.println(" reverse");
    motorSpeed(m, -MOTOR_PWM);
    delay(HOLD_MS);

    motorSpeed(m, 0);
  }

  Serial.println("Pause");
  delay(400);
}
