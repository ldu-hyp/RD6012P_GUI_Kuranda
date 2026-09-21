# RD6012P GUI Kuranda

Native C++ / Qt 6 desktop controller for the RIDEN RD6012P programmable power supply.

## Version

v0.2.0 — low-latency acquisition update

Target environment:

- Windows 10/11
- Visual Studio 2022 / MSVC
- Qt 6.8 LTS
- Qt Creator
- CMake
- C++17

Required Qt modules:

- Core
- Gui
- Widgets
- SerialPort

No Qt Charts dependency is used. The graph is custom-painted.

## Current features

- RIDEN startup probe: `queryd\r\n`
- Modbus RTU over 115200 8N1
- CRC16 validation
- Product ID, serial number and firmware information
- RD6012P identification
- Set voltage
- Set current
- Output ON/OFF
- 6 A / 12 A current-range switching
- Live V / I / P meters
- Live V / I / P graph
- Input voltage
- Internal temperature
- CV / CC
- Protection status
- Preset and keypad-lock state
- Actual update-rate display
- Serial round-trip-time display
- User writes pre-empt background polling
- Timeout and one automatic retry

## v0.2 low-latency architecture

The original prototype read registers `0x0004..0x0029` on every cycle. That returned 81 bytes and also caused the GUI to refresh many controls on every measurement.

v0.2 uses multi-rate acquisition.

### High-rate meter path

The continuous hot path reads only:

| Register | Meaning |
| --- | --- |
| 0x000A | Output voltage |
| 0x000B | Output current |
| 0x000C..0x000D | Output power |

Request:

```
01 03 00 0A 00 04 ...
```

The Modbus response is only 13 bytes instead of 81 bytes.

In **Maximum — continuous** mode there is no artificial host-side polling delay. The next meter transaction is scheduled immediately after the previous transaction completes.

### Medium-rate status path

Every approximately 250 ms the application reads:

```
0x000E..0x0014
```

This updates:

- input voltage
- keypad lock
- protection state
- CV/CC
- output ON/OFF
- preset
- RD6012P 6 A / 12 A range

### Setpoint path

Every approximately 500 ms:

```
0x0008..0x0009
```

updates V-SET and I-SET.

### Temperature path

Every approximately 1000 ms:

```
0x0004..0x0005
```

updates the internal temperature.

Only one lower-rate transaction is inserted between meter transactions at a time, preventing long gaps in the graph.

## UI performance

The GUI thread never blocks on serial I/O.

QSerialPort and all Modbus scheduling run in a dedicated QThread.

The UI no longer blindly writes every label and spin box on every sample. Widgets are only updated when their displayed value changes. Set-point spin boxes are not overwritten while the user is editing them.

The graph render cadence is independent from the instrument sampling cadence. It renders at approximately 60 FPS, so the time axis scrolls smoothly while all plotted values remain real samples received from the RD6012P; no fake interpolation is performed.

## Acquisition modes

- Maximum — continuous: 0 ms host idle
- Fast: 20 ms host idle
- Balanced: 100 ms host idle
- Slow: 500 ms host idle

The **Actual update rate** and **Last round trip** fields show the real performance obtained from the connected power supply.

If the displayed update rate remains low in Maximum mode, the limiting factor is then primarily the RD6012P / USB-serial response latency rather than the GUI timer.

## RD6012P scaling

- Voltage: 0.001 V/LSB
- Current in 6 A range: 0.0001 A/LSB
- Current in 12 A range: 0.001 A/LSB
- Input voltage: 0.01 V/LSB
- Power: 32-bit value across 0x000C..0x000D, interpreted as 0.01 W/LSB

## Build in Qt Creator

1. Install Qt 6.8.x MSVC 2022 64-bit.
2. Install the Qt Serial Port module.
3. Open the root `CMakeLists.txt` in Qt Creator.
4. Select the Qt 6.8 / MSVC 2022 64-bit kit.
5. Configure.
6. Build.
7. Run.

## Hardware-test note

The project is specifically being developed against RD6012P V1.55 communication captures.

For first hardware tests, use conservative voltage/current limits and a non-critical load.

The public RD60xx documentation has historical disagreement around the power-register interpretation. This code currently uses the 32-bit interpretation across registers 0x000C..0x000D and should continue to be cross-checked against real RD6012P V1.55 measurements.
