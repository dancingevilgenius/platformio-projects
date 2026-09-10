# PlatformIO shared libraries

Central library folder for this repository (similar to Arduino IDE `sketchbook/libraries`).

Place libraries here when they are:

- Custom forks or local patches (not suitable for `lib_deps` as-is)
- Not available on the PlatformIO Registry
- Shared by multiple projects under `Examples/`, `Arduino/`, or `Tutorials and Books/`

## Layout

Each library is one subdirectory with a standard Arduino layout:

```
libraries/
└── MyLibrary/
    ├── library.properties
    ├── library.json          (optional; PlatformIO)
    └── src/
        └── ...
```

## Usage in a project

Include the shared config from the project `platformio.ini`, then extend the shared environment:

```ini
[platformio]
extra_configs = ../../platformio.shared.ini

[env:my_board]
extends = common
platform = espressif32
board = esp32dev
framework = arduino

; Registry libraries still go in lib_deps when possible:
; lib_deps =
;     bblanchon/ArduinoJson @ ^7.4.3
```

Adjust the `extra_configs` path for project depth:

| Project location | `extra_configs` path |
|------------------|----------------------|
| `Examples/MyProject/` | `../../platformio.shared.ini` |
| `Examples/ESP32/MyProject/` | `../../../platformio.shared.ini` |
| `Examples/RP2040/MyProject/` | `../../../platformio.shared.ini` |
| `Examples/STM32/MyProject/` | `../../../platformio.shared.ini` |
| `Examples/Rev4/MyProject/` | `../../../platformio.shared.ini` |
| `Examples/Rev3/MyProject/` | `../../../platformio.shared.ini` |
| `Robots/MiniSumo/MyProject/` | `../../../platformio.shared.ini` |
| `Robots/LineFollower/MyProject/` | `../../../platformio.shared.ini` |
| `Robots/ConeStalker/MyProject/` | `../../../platformio.shared.ini` |
| `Arduino/ESP32/MyProject/` | `../../../platformio.shared.ini` |
| `Tutorials and Books/.../MyProject/` | `../../../platformio.shared.ini` |

Project-specific libraries can still live in that project's `lib/` folder.

## Vendored libraries (repo root `libraries/`)

| Folder | Used for |
|--------|----------|
| `Serial_Controlled_Motor_Driver/` | SparkFun SCMD (custom fork; not from Registry) |
| `FS_MX1508/` | MX1508 / DRV8871 motor driver |
| `MedianFilterLib2/` | Median filter for distance sensors |
| `Fork_of_PS3_Controller_Host/` | PS3 controller over Bluetooth (ESP32) |
| `emakefun_line_tracker_v3/` | Acebott / I2C five-line tracker |
| `HUSKYLENS/` | DFRobot HuskyLens vision sensor |
| `SparkFun_Qwiic_Alphanumeric_Display_Arduino_Library/` | Qwiic alphanumeric display |
| `Adafruit_TCS34725/` | RGB color line sensor |
| `SparkFun_VL53L1X_4m_Laser_Distance_Sensor/` | VL53L1X ToF distance (ConeStalker / Rev4) |
| `NewPing/` | Ultrasonic ranging (ConeStalker NewPing3Sensors) |
| `Bas.Button/` | Debounced button callbacks (ConeStalker BasButtonArray) |
| `Bas.CallbackCaller/` | Dependency of Bas.Button |
| `Adafruit_GFX_Library/` | OLED graphics (ConeStalker ZioUltrasonicSensor) |
| `Adafruit_SSD1306/` | SSD1306 OLED driver |
| `Adafruit_BusIO/` | I2C/SPI helpers for Adafruit libs |
| `Adafruit_IS31FL3741_Library/` | QT Py 13x9 RGB matrix (MiniSumo) |
| `ArduinoJson/` | JSON for MiniSumo web UI |
| `AsyncTCP/` | ESP32 async TCP (ESP32Async 3.4.10) |
| `ESPAsyncWebServer/` | Async HTTP/WebSocket server (ESP32Async 3.11.0) |
| `DFRobot_MatrixLidar/` | DFRobot 8x8 matrix ToF lidar |

Projects with `extends = common` pick these up automatically via `lib_extra_dirs`.
