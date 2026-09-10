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

#define LEFT_DIR_FW   0
#define LEFT_DIR_RV   1
#define RIGHT_DIR_FW  0
#define RIGHT_DIR_RV  1

#define LEFT_MOTOR 1
#define RIGHT_MOTOR 0
int leftMotor256=0;
int rightMotor256=0;
#define RIGHT_FW_FACTOR   1.0f
#define LEFT_FW_FACTOR    0.94f
#define RIGHT_RV_FACTOR   0.94f
#define LEFT_RV_FACTOR    1.0f


// ------------------------------------------------------------
// WiFi Credential Rotation
// ------------------------------------------------------------
struct WifiCredential {
  const char* ssid;
  const char* password;
};

#define NUM_NETWORKS 1

WifiCredential wifiList[NUM_NETWORKS] = {
  //{ "TheMandalorian",  "6302201111" },
  //{"2WIRE543", "0058239804"},
  //{ "TheMandaloriKen", "asdf12346302201111" },
  {"Kajeet SmartSpot 9433", "smartspot2631" }
};

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

#define X_OFFSET 2
#define INVALID_VAL 4000
#define MAX_DIST 570

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

enum SendMode { MODE_FULL, MODE_BITMASK };
SendMode sendMode = MODE_FULL;

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

// ------------------------------------------------------------
// WiFi rotation connect (new version) + cache
// ------------------------------------------------------------
bool connectToWiFi() {
  Serial.println("Starting WiFi credential rotation...");

  cachedStatus = WL_DISCONNECTED;
  cachedIp = IPAddress(0,0,0,0);
  cachedRssi = 0;

  for (int i = 0; i < NUM_NETWORKS; i++) {
    Serial.print("Trying SSID: ");
    Serial.print(wifiList[i].ssid);

    WiFi.setAutoReconnect(false);
    WiFi.disconnect();
    delay(500);
    WiFi.mode(WIFI_STA);
    delay(500);
    WiFi.begin(wifiList[i].ssid, wifiList[i].password);
    WiFi.setAutoReconnect(true);

    int failCount = 0;
    while (WiFi.status() != WL_CONNECTED && failCount < 20) {
      delay(500);
      Serial.print(".");
      failCount++;
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nConnected!");
      pendingMessage = String("Connected to ") + wifiList[i].ssid;
      pendingSeverity = "success";

      cachedIp = WiFi.localIP();
      cachedRssi = WiFi.RSSI();
      cachedStatus = WL_CONNECTED;

      Serial.print("IP address: ");
      Serial.println(cachedIp);
      return true;
    }

    Serial.println("\nFailed to connect. Moving to next SSID...");
  }

  pendingMessage = "Failed to connect to any WiFi network";
  pendingSeverity = "error";

  cachedStatus = WL_DISCONNECTED;
  cachedIp = IPAddress(0,0,0,0);
  cachedRssi = 0;

  Serial.println("ERROR: Could not connect to ANY WiFi network.");
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
    String m = msg.substring(5);
    sendMode = (m == "BIT") ? MODE_BITMASK : MODE_FULL;
    Serial.print("sendMode:");
    Serial.println(sendMode);
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
  if (!LittleFS.begin()) {
    Serial.println("LittleFS mount failed.");
  } else {
    Serial.println("LittleFS mounted.");
  }

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
          return;
      }

      if (type == WS_EVT_DISCONNECT) {
          Serial.printf("WS: Client %u disconnected\n", client->id());
          return;
      }

      if (type == WS_EVT_DATA) {

          // Convert incoming data to string (works for text or binary)
          String msg;
          for (size_t i = 0; i < len; i++) msg += (char)data[i];

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

          // Only process commands if this was a text frame
          if (info->opcode == WS_TEXT) {
              handleCommand(msg, client);
          }
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

  // Root → DPad page (PSRAM)
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
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
    request->send(LittleFS, "/index_grid.html", "text/html");
  });

  server.on("/css/grid.css", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/css/grid.css", "text/css");
  });

  server.on("/js/grid.js", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/js/grid.js", "application/javascript");
  });

  // ---------------- DPAD PAGE ----------------
  server.on("/css/dpad.css", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/css/dpad.css", "text/css");
  });

  server.on("/js/dpad.js", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/js/dpad.js", "application/javascript");
  });

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
          leftMotor256=150;
          rightMotor256=150;
          myMotorDriver.setDrive( LEFT_MOTOR, LEFT_DIR_FW,  (int)(leftMotor256 * LEFT_FW_FACTOR));
          myMotorDriver.setDrive( RIGHT_MOTOR, RIGHT_DIR_FW, (int)(rightMotor256 * RIGHT_FW_FACTOR));
          Serial.println("Start Button");
        }
        if (action == "stop") {
          animationRunning = false;
          myMotorDriver.setDrive( LEFT_MOTOR, LEFT_DIR_FW,  (int)(0 * LEFT_FW_FACTOR));
          myMotorDriver.setDrive( RIGHT_MOTOR, RIGHT_DIR_FW, (int)(0 * RIGHT_FW_FACTOR));
          Serial.println("Stop Button");
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

  //Uncomment code for motor 1 inversion
  while ( myMotorDriver.busy() ); //Waits until the SCMD is available.
  myMotorDriver.inversionMode(LEFT_MOTOR, 1); //invert motor 1
  //myMotorDriver.inversionMode(RIGHT_MOTOR, 1); //invert motor 1

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
void loopAnimation();
void loopClientCleanup();
void printClientList();

void setup() {
  Serial.begin(115200);
  while(!Serial) delay(10);
  delay(2000);

  Serial.println("MiniSumo QT PY Pico LedMatrix and DFR8x8");

  connectToWiFi();
  setupI2C();
  setupLedMatrix();
  setupDFR8x8();
  setupWebServer();
  setupQwiicMotorDriver();
}

void loop() {
  loopMiniSumoOpponent();
  loopAnimation();
  loopClientCleanup();


  delay(20);
}



// ------------------------------------------------------------
// MINI-SUMO COLOR LOGIC — TRUE RGB
// ------------------------------------------------------------
void loopMiniSumoOpponent() {

  tof.getAllData(lidarGrid);

  for (uint8_t y = 0; y < 8; y++) {
    for (uint8_t x = 0; x < 8; x++) {

      int d_mm = lidarGrid[y * 8 + x];

      if (d_mm == INVALID_VAL || d_mm > MAX_DIST) {
        ledmatrix.drawPixel(x + X_OFFSET, y, 0);
        colorGrid[y][x] = 0;
        continue;
      }

      if (y <= 4) {
        if (d_mm > 0) {
          ledmatrix.drawPixel(x + X_OFFSET, y, ledmatrix.color565(0,255,0));
          colorGrid[y][x] = 0x00FF00;
        }
        continue;
      }

      if (y == 6) {
        if (d_mm > 0) {
          ledmatrix.drawPixel(x + X_OFFSET, y, ledmatrix.color565(255,255,0));
          colorGrid[y][x] = 0xFFFF00;
        }
        continue;
      }

      if (y == 7) {
        if (d_mm > 0) {
          ledmatrix.drawPixel(x + X_OFFSET, y, ledmatrix.color565(255,0,0));
          colorGrid[y][x] = 0xFF0000;
        }
        continue;
      }

      ledmatrix.drawPixel(x + X_OFFSET, y, 0);
      colorGrid[y][x] = 0;
    }
  }
}

// ------------------------------------------------------------
// Send full 8×8 grid (128 bytes)
// ------------------------------------------------------------
  
void sendFullGrid() {
    // 80 cells × 4 bytes = 320 bytes
    const size_t frameSize = 64 * 4;

    // Broadcast the entire RGB grid buffer
    //ws.binaryAll((uint8_t*)colorGrid, frameSize);
    safeBinaryAll((uint8_t*)colorGrid, frameSize);
}

// ------------------------------------------------------------
// Send compact 64-bit bitmask
// ------------------------------------------------------------
void sendBitGrid() {
  uint64_t bits = 0;

  for (int y = 0; y < 8; y++) {
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


