//  Mini Sumo Backend — TRUE RGB VERSION
//  Sends real 0xRRGGBB colors to the front-end

#include <Arduino.h>
#include <ArduinoJson.h>


#include "Wire.h"
#include "DFRobot_MatrixLidar.h"
#include <Adafruit_IS31FL3741.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
//#include <ArduinoJson.h>
#include <map>
#include <stdint.h>
#include "SCMD.h"
#include "SCMD_config.h" //Contains #defines for common SCMD register names and values

// Sparkfun QWIIC Serial Controlled Motor Driver 
SCMD myMotorDriver; //This creates the main object of one motor driver and connected peripherals.

uint8_t LEFT_DIR_FW  = 0;
uint8_t LEFT_DIR_RV  = 1;
uint8_t RIGHT_DIR_FW = 0;
uint8_t RIGHT_DIR_RV = 1;

uint8_t LEFT_MOTOR  = 1;
uint8_t RIGHT_MOTOR = 0;
int leftMotor256 = 0;
int rightMotor256 = 0;
int motorPowerPercent = 25;
float RIGHT_FW_FACTOR = 1.0f;
float LEFT_FW_FACTOR  = 1.0f;
float RIGHT_RV_FACTOR = 1.0f;
float LEFT_RV_FACTOR  = 1.0f;
bool leftMotorInvert  = true;
bool rightMotorInvert = false;

// 8 sensor columns, x = 0..7. Left is 0..4. Right is 5..7.
// Middle is the two center columns, used to recognize an opponent straight ahead.
static const int COL_LEFT_LAST = 4;
static const int COL_RIGHT_FIRST = 5;
static const int COL_MIDDLE_FIRST = 3;
static const int COL_MIDDLE_LAST = 4;
static const int RED_OVERRIDE_MIN = 2;
static const int FRONT_GREEN_MIN = 4;
// Extra green hits on one side that unload the inside wheel. Past this, it reverses.
static const int OPPONENT_TURN_SCALE = 8;
// Fraction of full power taken off the inside wheel.
// Yellow removes a quarter, a slight arc. Red removes 1.5x, so the inside wheel reverses.
static const int YELLOW_TURN_NUM = 1;
static const int YELLOW_TURN_DEN = 4;
static const int RED_TURN_NUM = 3;
static const int RED_TURN_DEN = 2;
static const unsigned long MATCH_COUNTDOWN_MS = 5000;

enum MatchPhase { MATCH_IDLE, MATCH_COUNTDOWN, MATCH_RUN };
MatchPhase matchPhase = MATCH_IDLE;
unsigned long countdownStartMs = 0;

enum StartSide { START_CENTER, START_LEFT, START_RIGHT };
StartSide startSide = START_CENTER;
static const int SEARCH_POWER = (255 * 50) / 100;

enum MatchDrive { DRIVE_STOP, DRIVE_FORWARD, DRIVE_TURN_LEFT, DRIVE_TURN_RIGHT, DRIVE_REVERSE };
MatchDrive lastMatchDrive = DRIVE_STOP;

struct RingSense {
  int greenLeft;
  int greenRight;
  int greenMiddle;
  int greenTotal;
  int redLeft;
  int redRight;
  int redTotal;
  int yellowLeft;
  int yellowRight;
  int yellowTotal;
};
RingSense ringSense;


// ------------------------------------------------------------
// WiFi Credential Rotation
// ------------------------------------------------------------
struct WifiCredential {
  const char* ssid;
  const char* password;
};

#define NUM_NETWORKS 3

WifiCredential wifiList[NUM_NETWORKS] = {
  //{ "RobotStormClout",  "6302201111" },
  { "RobotStormCloud",  "6302201111" },
  { "2WIRE543", "0058239804" },
  { "TheMandaloriKen", "asdf12346302201111" },
  //{ "TheMandaloriKen", "asdf12346302201111" },
  //{"Kajeet SmartSpot 9433", "smartspot2631" }
};

String lastGoodSsid;
String lastGoodPassword;

String pendingMessage = "";
String pendingSeverity = "info";

// cached WiFi state (F2)
IPAddress cachedIp;
int cachedRssi = 0;
wl_status_t cachedStatus = WL_DISCONNECTED;




// ------------------------------------------------------------
// Hardware
// ------------------------------------------------------------
Adafruit_IS31FL3741_QT ledmatrix;

int X_OFFSET = 2;
int INVALID_VAL = 4000;
int MAX_DIST = 570;
int edgeRedMinCm = 10;     // bottom row (y = 7)
int edgeYellowMinCm = 15;  // next row up (y = 6)
int opponentMaxCm = 57;    // green rows, y = 0..4

// 8 rows × 10 columns = 80 RGB cells
uint32_t colorGrid[8][8] = {0};


DFRobot_MatrixLidar_I2C tof(0x33, &Wire1);
uint16_t lidarGrid[64];

// ------------------------------------------------------------
// Web Server + WebSocket
// ------------------------------------------------------------
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

std::map<uint32_t, String> clientUserAgents;

char* htmlBuffer = nullptr;
size_t htmlSize = 0;

bool animationRunning = true;

unsigned long lastAnimationTime = 0;
unsigned long lastClientCleanupTime = 0;


// NEW TEST animation state
bool testAnimationActive = false;
unsigned long testAnimStart = 0;
int testAnimStep = 0;
int testAnimCycles = 0;

unsigned long INTERVAL_ANIMATION = 100;       // default 10 FPS
unsigned long INTERVAL_CLIENT_CLEANUP = 5000; // 5 seconds

enum CalibratePhase { CALIB_IDLE, CALIB_FORWARD, CALIB_REVERSE };
CalibratePhase calibPhase = CALIB_IDLE;
unsigned long calibPhaseStart = 0;
bool calibUseLeft = false;
bool calibUseRight = false;
const char* calibDoneMessage = "CALIBRATE_DONE";
static const unsigned long CALIB_PHASE_MS = 3000;

enum SendMode { MODE_FULL, MODE_BITMASK };
SendMode sendMode = MODE_FULL;

// Both: every row. Edge: yellow (y=6) and red (y=7). Opponent: green (y<=4).
enum SensorView { SENSORS_BOTH, SENSORS_EDGE, SENSORS_OPPONENT };
SensorView sensorView = SENSORS_BOTH;

static bool rowIsProcessed(uint8_t y) {
  if (sensorView == SENSORS_EDGE) return y == 6 || y == 7;
  if (sensorView == SENSORS_OPPONENT) return y <= 4;
  return true;
}

static const char* sensorViewName() {
  if (sensorView == SENSORS_EDGE) return "edge";
  if (sensorView == SENSORS_OPPONENT) return "opponent";
  return "both";
}

// ------------------------------------------------------------
// D-Pad menu state
// ------------------------------------------------------------
const char* horizontalMenu[] = { "SPEED", "TURNING", "PROPORTIONAL", "INTEGRAL" };
int horizontalIndex = 0;
const int horizontalCount = 4;

const char* verticalMenu_M1[] = { "50", "60", "70", "80", "90", "100" };
const char* verticalMenu_M2[] = { "50", "60", "70", "80", "90", "100" };
const char* verticalMenu_M3[] = { "50", "60", "70", "80", "90" };
const char* verticalMenu_M4[] = { "0.1", "0.2", "0.3", "0.4", "0.5" };

const char** verticalMenus[] = {
  verticalMenu_M1,
  verticalMenu_M2,
  verticalMenu_M3,
  verticalMenu_M4
};

int verticalCounts[] = {
  sizeof(verticalMenu_M1) / sizeof(verticalMenu_M1[0]),
  sizeof(verticalMenu_M2) / sizeof(verticalMenu_M2[0]),
  sizeof(verticalMenu_M3) / sizeof(verticalMenu_M3[0]),
  sizeof(verticalMenu_M4) / sizeof(verticalMenu_M4[0])
};

int verticalIndex = 0;



// ------------------------------------------------------------
// PSRAM HTML loader
// ------------------------------------------------------------
bool loadHTMLToPSRAM(const char* filename) {
  if (!LittleFS.exists(filename)) {
    Serial.printf("ERROR: File %s not found\n", filename);
    return false;
  }

  File file = LittleFS.open(filename, "r");
  if (!file) {
    Serial.println("ERROR: Failed to open HTML file");
    return false;
  }

  htmlSize = file.size();
  htmlBuffer = (char*)ps_malloc(htmlSize + 1);

  if (!htmlBuffer) {
    Serial.println("ERROR: PSRAM allocation failed");
    file.close();
    return false;
  }

  file.readBytes(htmlBuffer, htmlSize);
  htmlBuffer[htmlSize] = '\0';
  file.close();

  Serial.printf("Loaded %s (%u bytes)\n", filename, (unsigned)htmlSize);
  return true;
}

static const char* SETTINGS_PATH = "/minisumo_settings.json";
static String wsCommand;

static int clampCm(int cm, int lo, int hi) {
  if (cm < lo) return lo;
  if (cm > hi) return hi;
  return cm;
}

static bool validPowerPercent(int percent) {
  return percent == 25 || percent == 50 || percent == 75 || percent == 100;
}

static int fullMotorPower() {
  return (255 * motorPowerPercent) / 100;
}

static int clampSigned(int value) {
  if (value > 255) return 255;
  if (value < -255) return -255;
  return value;
}

static int scaledSpeed(int magnitude, float factor) {
  int speed = (int)(magnitude * factor);
  if (speed < 0) speed = 0;
  if (speed > 255) speed = 255;
  return speed;
}

// Positive speed is forward. Negative speed is reverse.
void driveSigned(int left, int right) {
  left = clampSigned(left);
  right = clampSigned(right);

  if (left >= 0) {
    myMotorDriver.setDrive(LEFT_MOTOR, LEFT_DIR_FW, scaledSpeed(left, LEFT_FW_FACTOR));
  } else {
    myMotorDriver.setDrive(LEFT_MOTOR, LEFT_DIR_RV, scaledSpeed(-left, LEFT_RV_FACTOR));
  }

  if (right >= 0) {
    myMotorDriver.setDrive(RIGHT_MOTOR, RIGHT_DIR_FW, scaledSpeed(right, RIGHT_FW_FACTOR));
  } else {
    myMotorDriver.setDrive(RIGHT_MOTOR, RIGHT_DIR_RV, scaledSpeed(-right, RIGHT_RV_FACTOR));
  }

  leftMotor256 = left;
  rightMotor256 = right;
}

void stopAllMotors() {
  driveSigned(0, 0);
}

// Positive turn steers left (inside wheel slows, then reverses). Negative steers right.
void driveArc(int signedTurn) {
  const int power = fullMotorPower();
  int turn = signedTurn;
  const int limit = power * 2;
  if (turn > limit) turn = limit;
  if (turn < -limit) turn = -limit;

  if (turn > 0) driveSigned(power - turn, power);
  else if (turn < 0) driveSigned(power, power + turn);
  else driveSigned(power, power);
}

void applyCalibDrive(bool forward);

static void setMotorPowerPercent(int percent) {
  if (!validPowerPercent(percent)) percent = 25;
  motorPowerPercent = percent;
  if (matchPhase != MATCH_IDLE) return;
  if (calibPhase != CALIB_IDLE) {
    applyCalibDrive(calibPhase == CALIB_FORWARD);
    return;
  }
  if (animationRunning && (leftMotor256 > 0 || rightMotor256 > 0)) {
    leftMotor256 = fullMotorPower();
    rightMotor256 = fullMotorPower();
    myMotorDriver.setDrive(LEFT_MOTOR, LEFT_DIR_FW, (int)(leftMotor256 * LEFT_FW_FACTOR));
    myMotorDriver.setDrive(RIGHT_MOTOR, RIGHT_DIR_FW, (int)(rightMotor256 * RIGHT_FW_FACTOR));
  }
}

static void applyModeString(String m) {
  m.trim();
  m.toLowerCase();
  sendMode = (m.startsWith("1") || m.startsWith("bit")) ? MODE_BITMASK : MODE_FULL;
}

static void applySensorString(String view) {
  view.trim();
  view.toLowerCase();
  if (view.startsWith("edge")) sensorView = SENSORS_EDGE;
  else if (view.startsWith("opp")) sensorView = SENSORS_OPPONENT;
  else sensorView = SENSORS_BOTH;
}

void saveMiniSumoSettings() {
  JsonDocument doc;
  JsonObject left = doc["motors"]["left"].to<JsonObject>();
  left["channel"] = LEFT_MOTOR;
  left["dirFw"] = LEFT_DIR_FW;
  left["dirRv"] = LEFT_DIR_RV;
  left["fwFactor"] = LEFT_FW_FACTOR;
  left["rvFactor"] = LEFT_RV_FACTOR;
  left["invert"] = leftMotorInvert;

  JsonObject right = doc["motors"]["right"].to<JsonObject>();
  right["channel"] = RIGHT_MOTOR;
  right["dirFw"] = RIGHT_DIR_FW;
  right["dirRv"] = RIGHT_DIR_RV;
  right["fwFactor"] = RIGHT_FW_FACTOR;
  right["rvFactor"] = RIGHT_RV_FACTOR;
  right["invert"] = rightMotorInvert;
  doc["motors"]["powerPercent"] = motorPowerPercent;

  JsonObject lidar = doc["lidar"].to<JsonObject>();
  lidar["xOffset"] = X_OFFSET;
  lidar["invalidVal"] = INVALID_VAL;
  lidar["maxDist"] = MAX_DIST;
  lidar["edgeRedMinCm"] = edgeRedMinCm;
  lidar["edgeYellowMinCm"] = edgeYellowMinCm;
  lidar["opponentMaxCm"] = opponentMaxCm;

  JsonObject display = doc["display"].to<JsonObject>();
  display["mode"] = (sendMode == MODE_BITMASK) ? "bitmask" : "full";
  display["sensors"] = sensorViewName();

  JsonObject lastGood = doc["lastGoodConnection"].to<JsonObject>();
  lastGood["ssid"] = lastGoodSsid;
  lastGood["password"] = lastGoodPassword;

  if (doc.overflowed()) {
    Serial.println("ERROR: Settings JSON document overflowed");
    return;
  }

  File file = LittleFS.open(SETTINGS_PATH, "w");
  if (!file) {
    Serial.println("ERROR: Could not write settings file");
    return;
  }
  serializeJsonPretty(doc, file);
  file.flush();
  file.close();
  Serial.printf("Saved %s mode=%s sensors=%s red=%d yellow=%d opponentMax=%d power=%d\n",
                SETTINGS_PATH,
                (sendMode == MODE_BITMASK) ? "bitmask" : "full",
                sensorViewName(),
                edgeRedMinCm,
                edgeYellowMinCm,
                opponentMaxCm,
                motorPowerPercent);
}

static void applyMotorSettings(JsonObjectConst motor, uint8_t& channel, uint8_t& dirFw,
                               uint8_t& dirRv, float& fwFactor, float& rvFactor, bool& invert) {
  channel = motor["channel"] | channel;
  dirFw = motor["dirFw"] | dirFw;
  dirRv = motor["dirRv"] | dirRv;
  fwFactor = motor["fwFactor"] | fwFactor;
  rvFactor = motor["rvFactor"] | rvFactor;
  invert = motor["invert"] | invert;
}

void loadMiniSumoSettings() {
  if (!LittleFS.exists(SETTINGS_PATH)) {
    Serial.println("Settings file missing; writing defaults");
    saveMiniSumoSettings();
    return;
  }

  File file = LittleFS.open(SETTINGS_PATH, "r");
  if (!file) {
    Serial.println("ERROR: Failed to open settings file");
    return;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, file);
  file.close();
  if (err) {
    Serial.printf("ERROR: Settings JSON parse failed: %s\n", err.c_str());
    return;
  }

  if (doc["motors"]["left"].is<JsonObject>()) {
    applyMotorSettings(doc["motors"]["left"].as<JsonObjectConst>(),
                       LEFT_MOTOR, LEFT_DIR_FW, LEFT_DIR_RV,
                       LEFT_FW_FACTOR, LEFT_RV_FACTOR, leftMotorInvert);
  }
  if (doc["motors"]["right"].is<JsonObject>()) {
    applyMotorSettings(doc["motors"]["right"].as<JsonObjectConst>(),
                       RIGHT_MOTOR, RIGHT_DIR_FW, RIGHT_DIR_RV,
                       RIGHT_FW_FACTOR, RIGHT_RV_FACTOR, rightMotorInvert);
  }
  if (!doc["motors"]["powerPercent"].isNull()) {
    setMotorPowerPercent(doc["motors"]["powerPercent"].as<int>());
  }
  if (doc["lidar"].is<JsonObject>()) {
    JsonObjectConst lidar = doc["lidar"].as<JsonObjectConst>();
    X_OFFSET = lidar["xOffset"] | X_OFFSET;
    INVALID_VAL = lidar["invalidVal"] | INVALID_VAL;
    MAX_DIST = lidar["maxDist"] | MAX_DIST;
    edgeRedMinCm = clampCm(lidar["edgeRedMinCm"] | edgeRedMinCm, 10, 20);
    edgeYellowMinCm = clampCm(lidar["edgeYellowMinCm"] | edgeYellowMinCm, 15, 30);
    opponentMaxCm = clampCm(lidar["opponentMaxCm"] | opponentMaxCm, 15, 75);
  }
  if (doc["display"].is<JsonObject>()) {
    JsonObjectConst display = doc["display"].as<JsonObjectConst>();
    applyModeString(display["mode"] | "full");
    applySensorString(display["sensors"] | "both");
  }
  if (doc["lastGoodConnection"].is<JsonObject>()) {
    JsonObjectConst lastGood = doc["lastGoodConnection"].as<JsonObjectConst>();
    lastGoodSsid = lastGood["ssid"] | "";
    lastGoodPassword = lastGood["password"] | "";
  }

  Serial.printf("Loaded %s\n", SETTINGS_PATH);
  if (lastGoodSsid.length() > 0) {
    Serial.print("lastGoodConnection SSID: ");
    Serial.println(lastGoodSsid);
  }
}

bool littleFsMounted = false;

bool mountLittleFSAndLoadSettings() {
  if (littleFsMounted) return true;
  if (!LittleFS.begin()) {
    Serial.println("LittleFS mount failed.");
    return false;
  }
  littleFsMounted = true;
  Serial.println("LittleFS mounted.");
  loadMiniSumoSettings();
  return true;
}

// ------------------------------------------------------------
// WiFi rotation connect (new version) + cache
// ------------------------------------------------------------
static const int WIFI_TRIES_PER_NETWORK = 3;
static const unsigned long WIFI_TRY_TIMEOUT_MS = 3000;
static const unsigned long WIFI_POLL_MS = 250;

bool tryWifiNetwork(const char* ssid, const char* password) {
  if (!ssid || ssid[0] == '\0') return false;

  for (int attempt = 1; attempt <= WIFI_TRIES_PER_NETWORK; attempt++) {
    Serial.print("  Attempt ");
    Serial.print(attempt);
    Serial.print("/");
    Serial.print(WIFI_TRIES_PER_NETWORK);
    Serial.println(" (3 seconds)...");

    WiFi.disconnect();
    delay(100);
    WiFi.begin(ssid, password);

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - start) < WIFI_TRY_TIMEOUT_MS) {
      delay(WIFI_POLL_MS);
      Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) return true;

    Serial.print("  Attempt ");
    Serial.print(attempt);
    Serial.print(" failed for ");
    Serial.println(ssid);
    WiFi.disconnect();
  }

  Serial.print(ssid);
  Serial.println(" not found after 3 tries.");
  return false;
}

void markWifiConnected(const String& ssid, const String& password, bool usedLastGood) {
  WiFi.setAutoReconnect(true);
  pendingMessage = String("Connected to ") + ssid;
  pendingSeverity = "success";
  cachedIp = WiFi.localIP();
  cachedRssi = WiFi.RSSI();
  cachedStatus = WL_CONNECTED;

  if (usedLastGood) {
    Serial.println("Successfully connected using lastGoodConnection");
  } else {
    Serial.print("Connected to ");
    Serial.println(ssid);
  }
  Serial.print("IP address: ");
  Serial.println(cachedIp);

  const bool changed = lastGoodSsid != ssid || lastGoodPassword != password;
  lastGoodSsid = ssid;
  lastGoodPassword = password;
  if (changed) {
    saveMiniSumoSettings();
  }
}

bool connectToWiFi() {
  Serial.println("Starting WiFi credential rotation...");

  cachedStatus = WL_DISCONNECTED;
  cachedIp = IPAddress(0, 0, 0, 0);
  cachedRssi = 0;

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);

  if (lastGoodSsid.length() > 0) {
    Serial.println();
    Serial.print("Trying lastGoodConnection first: ");
    Serial.println(lastGoodSsid);
    if (tryWifiNetwork(lastGoodSsid.c_str(), lastGoodPassword.c_str())) {
      markWifiConnected(lastGoodSsid.c_str(), lastGoodPassword.c_str(), true);
      return true;
    }
    Serial.println("lastGoodConnection failed. Falling back to wifiList.");
  }

  for (int i = 0; i < NUM_NETWORKS; i++) {
    if (lastGoodSsid.length() > 0 && lastGoodSsid == wifiList[i].ssid) {
      continue;
    }

    Serial.println();
    Serial.print("SSID ");
    Serial.print(i + 1);
    Serial.print("/");
    Serial.print(NUM_NETWORKS);
    Serial.print(": ");
    Serial.println(wifiList[i].ssid);

    if (tryWifiNetwork(wifiList[i].ssid, wifiList[i].password)) {
      markWifiConnected(wifiList[i].ssid, wifiList[i].password, false);
      return true;
    }

    Serial.println("Moving to next SSID.");
  }

  pendingMessage = "Failed to connect to any WiFi network";
  pendingSeverity = "error";
  cachedStatus = WL_DISCONNECTED;
  cachedIp = IPAddress(0, 0, 0, 0);
  cachedRssi = 0;

  Serial.println();
  Serial.println("===========================================");
  Serial.println("ERROR: WiFi connection failed for all networks:");
  if (lastGoodSsid.length() > 0) {
    Serial.print("  - lastGoodConnection: ");
    Serial.println(lastGoodSsid);
  }
  for (int i = 0; i < NUM_NETWORKS; i++) {
    Serial.print("  - ");
    Serial.println(wifiList[i].ssid);
  }
  Serial.println("===========================================");
  return false;
}

/* ------------------------------------------------------------
   WebSocket Safety Wrappers (B3‑A)
   ------------------------------------------------------------ */

void safeTextAll(const String& msg) {
  AsyncWebSocketMessageBuffer *buf = ws.makeBuffer(msg.length() + 1);
  if (!buf) return;
  memcpy(buf->get(), msg.c_str(), msg.length());
  buf->get()[msg.length()] = '\0';
  ws.textAll(buf);
}

void safeBinaryAll(const uint8_t* data, size_t len) {
  AsyncWebSocketMessageBuffer *buf = ws.makeBuffer(len);
  if (!buf) return;
  memcpy(buf->get(), data, len);
  ws.binaryAll(buf);
}

void safeTextClient(AsyncWebSocketClient* client, const String& msg) {
  if (!client) return;
  AsyncWebSocketMessageBuffer *buf = ws.makeBuffer(msg.length() + 1);
  if (!buf) return;
  memcpy(buf->get(), msg.c_str(), msg.length());
  buf->get()[msg.length()] = '\0';
  client->text(buf);
}

void safeBinaryClient(AsyncWebSocketClient* client, const uint8_t* data, size_t len) {
  if (!client) return;
  AsyncWebSocketMessageBuffer *buf = ws.makeBuffer(len);
  if (!buf) return;
  memcpy(buf->get(), data, len);
  client->binary(buf);
}

void safeCloseClient(AsyncWebSocketClient* client) {
  if (!client) return;
  client->close();
}

void safeCloseClientWithMessage(AsyncWebSocketClient* client, const String& msg) {
  if (!client) return;
  safeTextClient(client, msg);
  client->close();
}


// ------------------------------------------------------------
// I2C + Sensor Setup
// ------------------------------------------------------------
void setupI2C() {
  Wire1.begin();
}

void setupDFR8x8() {
  int count=0;
  while(tof.begin() != 0 ){
    Serial.print(".");
    count++;
    if(count > 10){
      Serial.println("DFR MatrixLidar not found. Exiting setupDFR8x8()");
      return;
    }
    delay(500);
  }
  Serial.println("DFR MatrixLidar found.");

  Serial.println("DFR MatrixLidar init starting...");
  count = 0;
  while(tof.setRangingMode(eMatrix_8X8) != 0){
    Serial.print(count);
    Serial.print(" ");
    ++count;
    if(count > 10){
      Serial.println("DFR MatrixLidar not initialized.");
      return;
    }
    delay(250);
  }

  Serial.println("DFR MatrixLidar initialized!");
}

void setupLedMatrix() {
  if (! ledmatrix.begin(IS3741_ADDR_DEFAULT, &Wire1)) {
    Serial.println("LED 13x9 Matrix not found");
    return;
  }

  Serial.println("LED 13x9 Matrix found!");

  ledmatrix.setLEDscaling(0xAA);
  ledmatrix.setGlobalCurrent(0xCC);
  ledmatrix.enable(true);

  ledmatrix.fill(0);
  delay(2000);
}

// ------------------------------------------------------------
// Command parsing
// ------------------------------------------------------------
void healthCheckWifi();
void healthCheckI2C();
void healthCheckLedMatrix();
void healthCheckDFR8x8();
void healthCheckWebServer();

void stopCalibMotors() {
  stopAllMotors();
}

void applyCalibDrive(bool forward) {
  if (calibUseLeft) {
    const uint8_t dir = forward ? LEFT_DIR_FW : LEFT_DIR_RV;
    const float factor = forward ? LEFT_FW_FACTOR : LEFT_RV_FACTOR;
    myMotorDriver.setDrive(LEFT_MOTOR, dir, (int)(fullMotorPower() * factor));
  } else {
    myMotorDriver.setDrive(LEFT_MOTOR, LEFT_DIR_FW, 0);
  }

  if (calibUseRight) {
    const uint8_t dir = forward ? RIGHT_DIR_FW : RIGHT_DIR_RV;
    const float factor = forward ? RIGHT_FW_FACTOR : RIGHT_RV_FACTOR;
    myMotorDriver.setDrive(RIGHT_MOTOR, dir, (int)(fullMotorPower() * factor));
  } else {
    myMotorDriver.setDrive(RIGHT_MOTOR, RIGHT_DIR_FW, 0);
  }
}

void startCalibrate(bool useLeft, bool useRight, const char* label, const char* doneMessage) {
  if (matchPhase != MATCH_IDLE) {
    Serial.println("Calibration ignored while a match is active");
    return;
  }
  if (calibPhase != CALIB_IDLE) {
    Serial.println("Calibration already running");
    return;
  }
  calibUseLeft = useLeft;
  calibUseRight = useRight;
  calibDoneMessage = doneMessage;
  calibPhase = CALIB_FORWARD;
  calibPhaseStart = millis();
  Serial.print(label);
  Serial.print(": forward ");
  Serial.print(motorPowerPercent);
  Serial.println("% for 3s");
  applyCalibDrive(true);
}

void startCalibrateSensor() {
  startCalibrate(true, true, "Calibrate Sensor", "CALIBRATE_SENSOR_DONE");
}

void startCalibrateDriveLeft() {
  startCalibrate(true, false, "Drive Left Side", "CALIBRATE_DRIVE_DONE");
}

void startCalibrateDriveRight() {
  startCalibrate(false, true, "Drive Right Side", "CALIBRATE_DRIVE_DONE");
}

void startCalibrateDriveBoth() {
  startCalibrate(true, true, "Drive Both Sides", "CALIBRATE_DRIVE_DONE");
}

void applyStartSide(const String& side) {
  if (side == "left") startSide = START_LEFT;
  else if (side == "right") startSide = START_RIGHT;
  else startSide = START_CENTER;
}

void beginMatchCountdown() {
  if (matchPhase != MATCH_IDLE) return;
  if (calibPhase != CALIB_IDLE) calibPhase = CALIB_IDLE;
  matchPhase = MATCH_COUNTDOWN;
  countdownStartMs = millis();
  lastMatchDrive = DRIVE_STOP;
  stopAllMotors();
  Serial.print("Start: 5 second countdown, side ");
  Serial.println(startSide == START_LEFT ? "left" : startSide == START_RIGHT ? "right" : "center");
}

void endMatch() {
  matchPhase = MATCH_IDLE;
  if (calibPhase != CALIB_IDLE) calibPhase = CALIB_IDLE;
  lastMatchDrive = DRIVE_STOP;
  stopAllMotors();
  Serial.println("Stop: motors off");
}

void loopCalibrate() {
  if (matchPhase != MATCH_IDLE) return;
  if (calibPhase == CALIB_IDLE) return;
  if (millis() - calibPhaseStart < CALIB_PHASE_MS) return;

  if (calibPhase == CALIB_FORWARD) {
    calibPhase = CALIB_REVERSE;
    calibPhaseStart = millis();
    Serial.print("Calibration: reverse ");
    Serial.print(motorPowerPercent);
    Serial.println("% for 3s");
    applyCalibDrive(false);
    return;
  }

  stopCalibMotors();
  calibPhase = CALIB_IDLE;
  Serial.println("Calibration: done");
  safeTextAll(calibDoneMessage);
}

void handleCommand(const String& msg, AsyncWebSocketClient* client) {

    if (msg == "TEST") {
        testAnimationActive = true;
        testAnimStart = millis();
        testAnimStep = 0;
        testAnimCycles = 0;
        Serial.println("Starting test animation");
        return;
    }

  if (msg.startsWith("UA:")) {
    clientUserAgents[client->id()] = msg.substring(3);
    return;
  }

  if (msg.startsWith("RUN:")) {
    animationRunning = msg.substring(4).toInt();
  }
  else if (msg.startsWith("FPS:")) {
    int fps = msg.substring(4).toInt();
    fps = constrain(fps, 1, 60);
    INTERVAL_ANIMATION = 1000UL / fps;
    Serial.print("FPS:");
    Serial.println(fps);
  }
  else if (msg.startsWith("MODE:")) {
    applyModeString(msg.substring(5));
    saveMiniSumoSettings();
    Serial.print("sendMode:");
    Serial.println(sendMode == MODE_BITMASK ? "bitmask" : "full");
  }
  else if (msg.startsWith("SENSORS:")) {
    applySensorString(msg.substring(8));
    saveMiniSumoSettings();
    Serial.print("sensorView:");
    Serial.println(sensorViewName());
  }
  else if (msg.startsWith("EDGE_RED_CM:")) {
    edgeRedMinCm = clampCm(msg.substring(msg.indexOf(':') + 1).toInt(), 10, 20);
    saveMiniSumoSettings();
    Serial.print("edgeRedMinCm:");
    Serial.println(edgeRedMinCm);
  }
  else if (msg.startsWith("EDGE_YELLOW_CM:") || msg.indexOf("YELLOW") >= 0) {
    edgeYellowMinCm = clampCm(msg.substring(msg.indexOf(':') + 1).toInt(), 15, 30);
    saveMiniSumoSettings();
    Serial.print("edgeYellowMinCm:");
    Serial.println(edgeYellowMinCm);
  }
  else if (msg.startsWith("OPP_MAX_CM:")) {
    opponentMaxCm = clampCm(msg.substring(msg.indexOf(':') + 1).toInt(), 15, 75);
    saveMiniSumoSettings();
    Serial.print("opponentMaxCm:");
    Serial.println(opponentMaxCm);
  }
  else if (msg.startsWith("SNACK:")) {
    int p1 = msg.indexOf(':', 6);
    if (p1 > 0) {
      String type = msg.substring(6, p1);
      String text = msg.substring(p1 + 1);
      String payload = "SNACK:" + type + ":" + text;
      safeTextAll(payload);
    }
  }
  else if (msg == "CALIBRATE_DRIVE_LEFT") {
    startCalibrateDriveLeft();
  }
  else if (msg == "CALIBRATE_DRIVE_RIGHT") {
    startCalibrateDriveRight();
  }
  else if (msg == "CALIBRATE" || msg == "CALIBRATE_DRIVE" || msg == "CALIBRATE_DRIVE_BOTH") {
    startCalibrateDriveBoth();
  }
  else if (msg == "CALIBRATE_SENSOR") {
    startCalibrateSensor();
  }
  else if (msg == "HEALTHCHECK") {
    Serial.println("HEALTHCHECK command received");
    healthCheckWifi();
    healthCheckI2C();
    healthCheckLedMatrix();
    healthCheckDFR8x8();
    healthCheckWebServer();
  } 
}

// ------------------------------------------------------------
// Web Server + WebSocket setup
// ------------------------------------------------------------
void setupWebServer() {

  // ------------------------------------------------------------
  // 1. Mount LittleFS
  // ------------------------------------------------------------
  mountLittleFSAndLoadSettings();

  // ------------------------------------------------------------
  // 2. Register WebSocket handler FIRST (critical)
  // ------------------------------------------------------------
  ws.onEvent([](AsyncWebSocket *server,
                AsyncWebSocketClient *client,
                AwsEventType type,
                void *arg,
                uint8_t *data,
                size_t len)
  {
      AwsFrameInfo *info = (AwsFrameInfo*)arg;

      if (type == WS_EVT_CONNECT) {
          Serial.printf("WS: Client %u connected | IP: %s\n",
                        client->id(),
                        client->remoteIP().toString().c_str());
          safeTextClient(client, String("EDGE_MIN:") + edgeRedMinCm + "," + edgeYellowMinCm + "," + opponentMaxCm);
          safeTextClient(client, String("MODE:") + (sendMode == MODE_BITMASK ? "1" : "0"));
          safeTextClient(client, String("SENSORS:") + sensorViewName());
          return;
      }

      if (type == WS_EVT_DISCONNECT) {
          Serial.printf("WS: Client %u disconnected\n", client->id());
          return;
      }

      if (type == WS_EVT_DATA) {
          const bool textFrame = info->message_opcode == WS_TEXT || info->opcode == WS_TEXT;
          if (!textFrame) return;

          if (info->index == 0) wsCommand = "";
          for (size_t i = 0; i < len; i++) {
            char c = (char)data[i];
            if (c != '\0') wsCommand += c;
          }
          const bool complete = info->final && (info->index + len) >= info->len;
          if (!complete) return;

          String msg = wsCommand;
          wsCommand = "";
          msg.trim();

          // Handle PING:<id> for latency measurement
          if (msg.startsWith("PING:")) {
              String id = msg.substring(5);
              safeTextClient(client, "PONG:" + id);
              return;
          }
          // Respond to plain PING (no ID)
          if (msg == "PING") {
              safeTextClient(client, "PONG");
              return;
          }

          handleCommand(msg, client);
      }
  });

  // Attach WebSocket handler BEFORE any routes
  server.addHandler(&ws);

  // ------------------------------------------------------------
  // 3. Start server BEFORE defining routes (critical)
  // ------------------------------------------------------------
  server.begin();
  Serial.println("Web server started.");

  // ------------------------------------------------------------
  // 4. Load DPad HTML into PSRAM
  // ------------------------------------------------------------
  if (!loadHTMLToPSRAM("/index_dpad.html")) {
    Serial.println("FATAL: Could not load /index_dpad.html");
  }

  // ------------------------------------------------------------
  // 5. Define routes
  // ------------------------------------------------------------

  // Root → Autonomous / grid page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/index_grid.html", "text/html");
    response->addHeader("Cache-Control", "no-cache");
    request->send(response);
  });

  // ---------------- DPAD PAGE ----------------
  server.on("/index_dpad.html", HTTP_GET, [](AsyncWebServerRequest *request) {
      if (!htmlBuffer || htmlSize == 0) {
          request->send(500, "text/plain", "HTML not loaded");
          return;
      }
      AsyncWebServerResponse *response =
          request->beginResponse(200, "text/html",
                                (const uint8_t*)htmlBuffer, (size_t)htmlSize);
      response->addHeader("Cache-Control", "no-cache");
      request->send(response);
  });

  // ---------------- GRID PAGE ----------------
  server.on("/index_grid.html", HTTP_GET, [](AsyncWebServerRequest *request) {
    AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/index_grid.html", "text/html");
    response->addHeader("Cache-Control", "no-cache");
    request->send(response);
  });

  server.on("/css/grid.css", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/css/grid.css", "text/css");
  });

  server.on("/js/grid.js", HTTP_GET, [](AsyncWebServerRequest *request) {
    AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/js/grid.js", "application/javascript");
    response->addHeader("Cache-Control", "no-cache");
    request->send(response);
  });

  // ---------------- DPAD PAGE ----------------
  server.on("/css/dpad.css", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/css/dpad.css", "text/css");
  });

  server.on("/js/dpad.js", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/js/dpad.js", "application/javascript");
  });

  server.on("/settings", HTTP_GET, [](AsyncWebServerRequest *request) {
    JsonDocument doc;
    doc["mode"] = (sendMode == MODE_BITMASK) ? "bitmask" : "full";
    doc["sensors"] = sensorViewName();
    doc["edgeRedMinCm"] = edgeRedMinCm;
    doc["edgeYellowMinCm"] = edgeYellowMinCm;
    doc["opponentMaxCm"] = opponentMaxCm;
    doc["powerPercent"] = motorPowerPercent;
    String body;
    serializeJson(doc, body);
    request->send(200, "application/json", body);
  });

  server.on(
    "/settings",
    HTTP_POST,
    [](AsyncWebServerRequest *request) {},
    NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      static String body;
      if (index == 0) body = "";
      for (size_t i = 0; i < len; i++) body += (char)data[i];
      if (index + len < total) return;

      JsonDocument doc;
      if (deserializeJson(doc, body)) {
        request->send(400, "application/json", "{\"error\":\"bad json\"}");
        return;
      }

      if (!doc["mode"].isNull()) applyModeString(doc["mode"].as<String>());
      if (!doc["sensors"].isNull()) applySensorString(doc["sensors"].as<String>());
      if (!doc["edgeRedMinCm"].isNull()) {
        edgeRedMinCm = clampCm(doc["edgeRedMinCm"].as<int>(), 10, 20);
      }
      if (!doc["edgeYellowMinCm"].isNull()) {
        edgeYellowMinCm = clampCm(doc["edgeYellowMinCm"].as<int>(), 15, 30);
      }
      if (!doc["opponentMaxCm"].isNull()) {
        opponentMaxCm = clampCm(doc["opponentMaxCm"].as<int>(), 15, 75);
      }
      if (!doc["powerPercent"].isNull()) {
        setMotorPowerPercent(doc["powerPercent"].as<int>());
      }
      saveMiniSumoSettings();
      request->send(200, "application/json", "{\"ok\":true}");
    }
  );

  // ---------------- CONTROLLER ENDPOINT ----------------
  server.on(
    "/controller",
    HTTP_POST,
    [](AsyncWebServerRequest *request) {},
    NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {

      String body;
      body.reserve(total);
      for (size_t i = 0; i < len; i++) body += (char)data[i];

      JsonDocument doc;
      if (deserializeJson(doc, body)) {
        request->send(400, "application/json", "{\"error\":\"bad json\"}");
        return;
      }

      if (doc["direction"].is<const char*>()) {
        String direction = doc["direction"].as<String>();

        if (direction == "left") {
          Serial.println("Left Button - rotate left");          
          horizontalIndex--;
          if (horizontalIndex < 0) horizontalIndex = horizontalCount - 1;
          verticalIndex = 0;
          myMotorDriver.setDrive( LEFT_MOTOR, LEFT_DIR_RV,  (int)(100 * LEFT_FW_FACTOR));
          myMotorDriver.setDrive( RIGHT_MOTOR, RIGHT_DIR_FW, (int)(100 * RIGHT_FW_FACTOR));
        }
        else if (direction == "right") {
          Serial.println("Right Button - rotate right");          
          horizontalIndex++;
          if (horizontalIndex >= horizontalCount) horizontalIndex = 0;
          verticalIndex = 0;
          myMotorDriver.setDrive( LEFT_MOTOR, LEFT_DIR_FW,  (int)(100 * LEFT_FW_FACTOR));
          myMotorDriver.setDrive( RIGHT_MOTOR, RIGHT_DIR_RV, (int)(100 * RIGHT_FW_FACTOR));
        }
        else if (direction == "up") {
          Serial.println("Up Button - straight forward");          
          verticalIndex--;
          if (verticalIndex < 0) verticalIndex = verticalCounts[horizontalIndex] - 1;
          myMotorDriver.setDrive( LEFT_MOTOR, LEFT_DIR_FW,  (int)(100 * LEFT_FW_FACTOR));
          myMotorDriver.setDrive( RIGHT_MOTOR, RIGHT_DIR_FW, (int)(100 * RIGHT_FW_FACTOR));
        }
        else if (direction == "down") {
          Serial.println("Down Button - reverse straight back");
          verticalIndex++;
          if (verticalIndex >= verticalCounts[horizontalIndex]) verticalIndex = 0;
          myMotorDriver.setDrive( LEFT_MOTOR, LEFT_DIR_RV,  (int)(100 * LEFT_FW_FACTOR));
          myMotorDriver.setDrive( RIGHT_MOTOR, RIGHT_DIR_RV, (int)(100 * RIGHT_FW_FACTOR));
        } else if(direction == "center"){
          Serial.println("OK Button - stop motors");
          myMotorDriver.setDrive( LEFT_MOTOR, LEFT_DIR_RV,  (int)(0));
          myMotorDriver.setDrive( RIGHT_MOTOR, RIGHT_DIR_RV, (int)(0));
        }        
      }

      if (doc["action"].is<const char*>()) {
        String action = doc["action"].as<String>();
        if (action == "start"){
          animationRunning = true;
          String side = "center";
          if (doc["startingSide"].is<const char*>()) side = doc["startingSide"].as<String>();
          side.trim();
          side.toLowerCase();
          applyStartSide(side);
          beginMatchCountdown();
        }
        if (action == "stop") {
          animationRunning = false;
          endMatch();
        }
        if (action == "calibrateDriveLeft") {
          startCalibrateDriveLeft();
        }
        if (action == "calibrateDriveRight") {
          startCalibrateDriveRight();
        }
        if (action == "calibrateDrive" || action == "calibrateDriveBoth") {
          startCalibrateDriveBoth();
        }
        if (action == "calibrateSensor") {
          startCalibrateSensor();
        }
      }

      JsonDocument response;
      response["horiz"] = horizontalMenu[horizontalIndex];
      response["vert"]  = verticalMenus[horizontalIndex][verticalIndex];

      if (pendingMessage.length() > 0) {
        response["message"]  = pendingMessage;
        response["severity"] = pendingSeverity;
        pendingMessage = "";
      }

      String out;
      serializeJson(response, out);
      request->send(200, "application/json", out);
    }
  );
}

void setupQwiicMotorDriver(){
  //***** Configure the Motor Driver's Settings *****//
  //  .commInter face is I2C_MODE 
  myMotorDriver.settings.commInterface = I2C_MODE;

  //  set address if I2C configuration selected with the config jumpers
  myMotorDriver.settings.I2CAddress = 0x5D; //config pattern is "1000" (default) on board for address 0x5D

  //  set chip select if SPI selected with the config jumpers
  myMotorDriver.settings.chipSelectPin = 10;

  //*****initialize the driver get wait for idle*****//
  while ( myMotorDriver.begin() != 0xA9 ) //Wait until a valid ID word is returned
  {
    Serial.print("begin() return word ");
    Serial.print(myMotorDriver.begin(), HEX);
    Serial.print(" : ");
    Serial.println( "ID mismatch, trying again" );    
    delay(500);
  }
  Serial.println( "ID matches 0xA9" );

  //  Check to make sure the driver is done looking for peripherals before beginning
  Serial.print("Waiting for enumeration...");
  while ( myMotorDriver.ready() == false );
  Serial.println("Done.");
  Serial.println();

  //*****Set application settings and enable driver*****//

  while ( myMotorDriver.busy() ); //Waits until the SCMD is available.
  myMotorDriver.inversionMode(LEFT_MOTOR, leftMotorInvert ? 1 : 0);
  while ( myMotorDriver.busy() );
  myMotorDriver.inversionMode(RIGHT_MOTOR, rightMotorInvert ? 1 : 0);

  while ( myMotorDriver.busy() )
    ; // Do nothing until driver is available

  myMotorDriver.enable(); //Enables the output driver hardware

  // Set both motors to zero speed.
  myMotorDriver.setDrive( LEFT_MOTOR, LEFT_DIR_FW,  (int)(0 * LEFT_FW_FACTOR));
  myMotorDriver.setDrive( RIGHT_MOTOR, RIGHT_DIR_FW, (int)(0 * RIGHT_FW_FACTOR));


  Serial.println("Exiting setupQwiicMotorDriver()");
}


// ------------------------------------------------------------
// Setup + Loop
// ------------------------------------------------------------
void loopMiniSumoOpponent();
void loopMatch();
void loopAnimation();
void loopClientCleanup();
void printClientList();

void setup() {
  Serial.begin(115200);
  while(!Serial) delay(10);
  delay(2000);

  Serial.println("MiniSumo QT PY Pico LedMatrix and DFR8x8");

  mountLittleFSAndLoadSettings();
  connectToWiFi();
  setupI2C();
  setupLedMatrix();
  setupDFR8x8();
  setupWebServer();
  setupQwiicMotorDriver();
}

void loop() {
  loopMiniSumoOpponent();
  loopMatch();
  loopAnimation();
  loopClientCleanup();
  loopCalibrate();


  delay(20);
}



// ------------------------------------------------------------
// MINI-SUMO COLOR LOGIC — TRUE RGB
// ------------------------------------------------------------
void loopMiniSumoOpponent() {

  tof.getAllData(lidarGrid);
  ringSense = RingSense{};

  for (uint8_t y = 0; y < 8; y++) {
    for (uint8_t x = 0; x < 8; x++) {
      int d_mm = lidarGrid[y * 8 + x];
      uint32_t color = 0;

      // Edge rows: off at or under the minimum, yellow/red once farther than it.
      if (y == 6 || y == 7) {
        const int minMm = ((y == 7) ? edgeRedMinCm : edgeYellowMinCm) * 10;
        if (d_mm > minMm) color = (y == 7) ? 0xFF0000 : 0xFFFF00;
      } else if (y <= 4) {
        const int maxMm = opponentMaxCm * 10;
        if (d_mm > 0 && d_mm <= maxMm) color = 0x00FF00;
      }

      if (color == 0x00FF00) {
        ringSense.greenTotal++;
        if (x <= COL_LEFT_LAST) ringSense.greenLeft++;
        if (x >= COL_RIGHT_FIRST) ringSense.greenRight++;
        if (x >= COL_MIDDLE_FIRST && x <= COL_MIDDLE_LAST) ringSense.greenMiddle++;
      } else if (color == 0xFF0000) {
        ringSense.redTotal++;
        if (x <= COL_LEFT_LAST) ringSense.redLeft++;
        if (x >= COL_RIGHT_FIRST) ringSense.redRight++;
      } else if (color == 0xFFFF00) {
        ringSense.yellowTotal++;
        if (x <= COL_LEFT_LAST) ringSense.yellowLeft++;
        if (x >= COL_RIGHT_FIRST) ringSense.yellowRight++;
      }

      if (!rowIsProcessed(y)) color = 0;
      colorGrid[y][x] = color;

      if (color == 0xFF0000) {
        ledmatrix.drawPixel(x + X_OFFSET, y, ledmatrix.color565(255, 0, 0));
      } else if (color == 0xFFFF00) {
        ledmatrix.drawPixel(x + X_OFFSET, y, ledmatrix.color565(255, 255, 0));
      } else if (color == 0x00FF00) {
        ledmatrix.drawPixel(x + X_OFFSET, y, ledmatrix.color565(0, 255, 0));
      } else {
        ledmatrix.drawPixel(x + X_OFFSET, y, 0);
      }
    }
  }
}

static void logMatchDrive(MatchDrive mode, const char* label) {
  if (lastMatchDrive == mode) return;
  lastMatchDrive = mode;
  Serial.printf("Match %s  yellow L=%d R=%d  red L=%d R=%d\n",
                label,
                ringSense.yellowLeft,
                ringSense.yellowRight,
                ringSense.redLeft,
                ringSense.redRight);
}

// Positive driveArc turns left, away from a heavier right side.
static void steerAwayFromGreaterSide(int leftHits, int rightHits, int turn, const char* band) {
  if (rightHits > leftHits) {
    logMatchDrive(DRIVE_TURN_LEFT, band);
    driveArc(turn);
    return;
  }
  logMatchDrive(DRIVE_TURN_RIGHT, band);
  driveArc(-turn);
}

static void steerTowardOpponent() __attribute__((unused));
static void steerTowardOpponent() {
  const int diff = ringSense.greenLeft - ringSense.greenRight;
  const int turn = (fullMotorPower() * abs(diff)) / OPPONENT_TURN_SCALE;
  if (ringSense.greenMiddle >= FRONT_GREEN_MIN) {
    logMatchDrive(DRIVE_FORWARD, "opponent in front");
    driveSigned(fullMotorPower(), fullMotorPower());
    return;
  }
  if (diff > 0) {
    logMatchDrive(DRIVE_TURN_LEFT, "opponent left");
    driveArc(turn);
  } else if (diff < 0) {
    logMatchDrive(DRIVE_TURN_RIGHT, "opponent right");
    driveArc(-turn);
  } else {
    logMatchDrive(DRIVE_FORWARD, "opponent centered");
    driveArc(0);
  }
}

// Outside wheel at 50%. Inside wheel at half of that when a side is selected.
void driveDefaultSearch() {
  const int power = SEARCH_POWER;
  if (startSide == START_LEFT) {
    logMatchDrive(DRIVE_TURN_LEFT, "search left at 50%");
    driveSigned(power / 2, power);
    return;
  }
  if (startSide == START_RIGHT) {
    logMatchDrive(DRIVE_TURN_RIGHT, "search right at 50%");
    driveSigned(power, power / 2);
    return;
  }
  logMatchDrive(DRIVE_FORWARD, "search forward at 50%");
  driveSigned(power, power);
}

// Opponent steering is kept in steerTowardOpponent() and is not called.
// Motors stay stopped until the countdown reaches zero.
void loopMatch() {
  if (matchPhase == MATCH_IDLE) return;

  if (matchPhase == MATCH_COUNTDOWN) {
    if ((millis() - countdownStartMs) < MATCH_COUNTDOWN_MS) {
      logMatchDrive(DRIVE_STOP, "countdown hold");
      stopAllMotors();
      return;
    }
    matchPhase = MATCH_RUN;
    lastMatchDrive = DRIVE_STOP;
    Serial.println("Countdown done, match running");
  }

  const int power = fullMotorPower();

  if (ringSense.redTotal >= RED_OVERRIDE_MIN) {
    if (ringSense.redLeft == ringSense.redRight) {
      logMatchDrive(DRIVE_REVERSE, "red edge tied, reverse");
      driveSigned(-power, -power);
      return;
    }
    steerAwayFromGreaterSide(ringSense.redLeft, ringSense.redRight,
                            (power * RED_TURN_NUM) / RED_TURN_DEN,
                            "red edge, sharp turn");
    return;
  }
  if (ringSense.yellowLeft != ringSense.yellowRight) {
    steerAwayFromGreaterSide(ringSense.yellowLeft, ringSense.yellowRight,
                            (power * YELLOW_TURN_NUM) / YELLOW_TURN_DEN,
                            "yellow edge, slight turn");
    return;
  }

  driveDefaultSearch();
}

// ------------------------------------------------------------
// Send full 8×8 grid (128 bytes)
// ------------------------------------------------------------
  
void sendFullGrid() {
    uint32_t frame[8][8];
    for (int y = 0; y < 8; y++) {
      for (int x = 0; x < 8; x++) {
        frame[y][x] = rowIsProcessed(y) ? colorGrid[y][x] : 0;
      }
    }

    safeBinaryAll((uint8_t*)frame, sizeof(frame));
}

// ------------------------------------------------------------
// Send compact 64-bit bitmask
// ------------------------------------------------------------
void sendBitGrid() {
  uint64_t bits = 0;

  for (int y = 0; y < 8; y++) {
    if (!rowIsProcessed(y)) continue;
    for (int x = 0; x < 8; x++) {
      if (colorGrid[y][x] != 0) {
        bits |= (uint64_t)1 << (y * 8 + x);
      }
    }
  }

  safeBinaryAll((uint8_t*)&bits, sizeof(bits));
}



// ------------------------------------------------------------
// Animation Loop
// ------------------------------------------------------------
void loopAnimation() {
  unsigned long now = millis();

  if (animationRunning && now - lastAnimationTime >= INTERVAL_ANIMATION) {
    lastAnimationTime = now;

    
    if (ws.count() > 0) {
      if (sendMode == MODE_FULL)
        sendFullGrid();
      else
        sendBitGrid();
    }
  }

}

// ------------------------------------------------------------
// HEALTH CHECKS — each sends a snackbar via WebSocket
// ------------------------------------------------------------

void healthCheckWifi() {
  if (cachedStatus == WL_CONNECTED) {
    safeTextAll("SNACK:success:WiFi OK (" + cachedIp.toString() + ")");
  } else {
    safeTextAll("SNACK:error:WiFi NOT connected");
  }
}

void healthCheckI2C() {
  Wire1.beginTransmission(0x00);
  uint8_t err = Wire1.endTransmission();

  if (err == 0 || err == 2) {
    safeTextAll("SNACK:success:I2C Bus OK");
  } else {
    safeTextAll("SNACK:error:I2C Bus Error");
  }
}

void healthCheckLedMatrix() {
  if (ledmatrix.begin(IS3741_ADDR_DEFAULT, &Wire1)) {
    safeTextAll("SNACK:success:LED Matrix OK");
  } else {
    safeTextAll("SNACK:error:LED Matrix NOT responding");
  }
}

void healthCheckDFR8x8() {
  uint16_t tempGrid[64];

  if ( tof.begin() == 0 && tof.getAllData(tempGrid) == 0) {
    safeTextAll("SNACK:success:DFR 8×8 Lidar OK");
  } else {
    safeTextAll("SNACK:error:DFR 8×8 Lidar NOT responding");
  }
}

void healthCheckWebServer() {
  if(htmlBuffer != nullptr){
    safeTextAll("SNACK:success:Web Server OK");
  } else {
    safeTextAll("SNACK:error:Web Server web page not found");
  }
}

// ------------------------------------------------------------
// Client Cleanup
// ------------------------------------------------------------
void loopClientCleanup() {
  unsigned long now = millis();

  if (now - lastClientCleanupTime > INTERVAL_CLIENT_CLEANUP) {
    lastClientCleanupTime = now;
    printClientList();
    // IMPORTANT: do NOT call ws.cleanupClients() here (F1)
  }
}

// ------------------------------------------------------------
// Device Parsing Helpers
// ------------------------------------------------------------
String parseDeviceName(const String& ua) {
  String u = ua;
  u.toLowerCase();

  if (u.indexOf("iphone") >= 0) return "iPhone (iOS)";
  if (u.indexOf("ipad") >= 0) return "iPad (iOS)";
  if (u.indexOf("mac os") >= 0 || u.indexOf("macintosh") >= 0) return "Mac";

  if (u.indexOf("android") >= 0) {
    if (u.indexOf("sm-s92") >= 0) return "Samsung Galaxy S24 Ultra";
    if (u.indexOf("sm-s91") >= 0) return "Samsung Galaxy S24";
    if (u.indexOf("pixel") >= 0) return "Google Pixel";
    return "Android Device";
  }

  if (u.indexOf("windows nt") >= 0) return "Windows PC";
  if (u.indexOf("linux") >= 0) return "Linux PC";

  return "Unknown Device";
}

String parseBrowser(const String& ua) {
  String u = ua;
  u.toLowerCase();

  if (u.indexOf("chrome/") >= 0) return "Chrome";
  if (u.indexOf("safari/") >= 0 && u.indexOf("chrome") < 0) return "Safari";
  if (u.indexOf("firefox/") >= 0) return "Firefox";
  if (u.indexOf("edg/") >= 0) return "Edge";

  return "Unknown Browser";
}

// ------------------------------------------------------------
// Client List Debug
// ------------------------------------------------------------
void printClientList() {
  String hostIP = cachedIp.toString();
  int32_t rssi = cachedRssi;

  Serial.printf("---- WebSocket Clients (Host: %s | RSSI: %d dBm) ----\n",
                hostIP.c_str(), rssi);

  for (AsyncWebSocketClient& c : ws.getClients()) {
    AsyncWebSocketClient* client = &c;

    uint32_t cid = client->id();
    IPAddress ip = client->remoteIP();

    String ua = clientUserAgents.count(cid) ? clientUserAgents[cid] : "Unknown";
    String device = parseDeviceName(ua);
    String browser = parseBrowser(ua);

    if (client->status() == WS_CONNECTED) {
      Serial.printf(
        "Client %u: CONNECTED | IP: %s | Device: %s | Browser: %s\n",
        cid,
        ip.toString().c_str(),
        device.c_str(),
        browser.c_str()
      );
    } else {
      Serial.printf(
        "Client %u: NOT CONNECTED (closing) | IP: %s | Device: %s | Browser: %s\n",
        cid,
        ip.toString().c_str(),
        device.c_str(),
        browser.c_str()
      );
      safeCloseClient(client);
    }
  }

  Serial.printf("Active client count: %u\n", ws.count());
  Serial.println("-------------------------------------------");
}


