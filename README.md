# RD6012P GUI Kuranda

A native C++ / Qt 6 desktop control application for the RIDEN RD6012P programmable DC power supply.

## Version

First functional prototype: v0.1.0

Target environment:

- Windows 10/11
- Visual Studio 2022 / MSVC
- Qt 6.8 LTS
- Qt Creator
- CMake
- C++17

Qt modules required:

- Core
- Gui
- Widgets
- SerialPort

No Qt Charts dependency is required. The real-time plot is custom-painted to keep rendering overhead small.

## First-version features

- 115200 8N1 serial connection
- RIDEN startup probe: queryd + CR/LF
- Modbus RTU CRC16 framing
- Reads product ID, serial number and firmware version
- RD6012P product-ID recognition
- Fast asynchronous polling of registers 0x0004..0x0029
- Live output voltage, current and power
- Live input voltage, internal temperature, CV/CC, protection, preset and keypad lock
- RD6012P 6 A / 12 A current-range handling
- Set voltage
- Set current
- Output ON/OFF
- Current-range switching
- User-selectable polling delay
- Three-lane live graph for voltage/current/power
- Serial I/O runs in a dedicated QThread
- User write commands have priority over background polling
- One outstanding Modbus transaction at a time, with timeout and one retry

## Build with Qt Creator

1. Install Qt 6.8.x MSVC 2022 64-bit and the Qt Serial Port module.
2. Open CMakeLists.txt in Qt Creator.
3. Select a Qt 6.8 MSVC 2022 64-bit kit.
4. Configure the project.
5. Build and run.

## Protocol notes

The application is tuned for RD6012P.

Observed/used register layout:

| Register | Meaning |
| --- | --- |
| 0 | Product ID |
| 1..2 | Serial number |
| 3 | Firmware version |
| 4..7 | Internal temperature |
| 8 | Voltage set |
| 9 | Current set |
| 10 | Output voltage |
| 11 | Output current |
| 12..13 | Power display value |
| 14 | Input voltage |
| 15 | Keypad lock |
| 16 | Protection |
| 17 | CV/CC |
| 18 | Output enable |
| 19 | Preset |
| 20 | RD6012P current range |
| 72 / 0x48 | Backlight |

RD6012P scaling used by this build:

- Voltage: 0.001 V/LSB
- Current, 6 A range: 0.0001 A/LSB
- Current, 12 A range: 0.001 A/LSB
- Input voltage: 0.01 V/LSB
- Power: 0.01 W/LSB on the combined 32-bit value in registers 12..13

## Low-latency design

The GUI thread never waits for serial I/O. QSerialPort, transaction scheduling, CRC checking and timeouts all run in a dedicated worker thread.

Polling is transaction-driven rather than blindly timer-driven: a new read is only started after the previous request completes. This prevents request overlap even if the device takes longer than the selected poll delay.

User writes are placed in a priority queue. A voltage/current/output command therefore runs before the next background state read, and the application requests an immediate verification poll after the write acknowledgement.

The default poll delay is 80 ms. Actual update rate is limited by the RD6012P response latency plus this delay; it never sends overlapping Modbus requests.

## Important

This is the first prototype and should initially be tested with a safe voltage/current limit and a non-critical load.

The public RD60xx protocol documentation has some historical disagreement around the power-related registers. This implementation uses the newer 32-bit power interpretation for registers 12..13. We will verify this against RD6012P V1.55 hardware capture data in the next iteration.

## Planned next iterations

- OVP/OCP configuration
- M0-M9 presets
- CSV recording/export
- Min/max/average statistics
- Configurable graph time window
- Graph zoom/pan and cursor readout
- Raw Modbus traffic console
- Auto reconnect
- Device clock and additional settings
- More detailed error/timeout statistics
