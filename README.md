# RD6012P GUI Kuranda

Native C++ / Qt 6 desktop controller for the RIDEN RD6012P programmable power supply.

## Version

v0.2.1 — unified low-latency polling

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

## v0.2.1 low-latency architecture

Real hardware testing showed an important RD6012P characteristic:

- a small Modbus transaction still takes about 119 ms round trip
- therefore splitting live data into several small reads reduces total update rate
- 1000 / 119 ms is only about 8.4 transactions/s before any additional overhead

v0.2.1 therefore uses one continuous live transaction instead of separate meter/status/setpoint/temperature reads.

### Unified high-rate read

Every live cycle reads:

```
0x0004..0x0014
```

This is 17 registers and includes:

- internal temperature
- V-SET / I-SET
- V-OUT / I-OUT
- output power
- input voltage
- keypad lock
- protection state
- CV / CC
- output ON / OFF
- preset
- 6 A / 12 A range

The Modbus response is 39 bytes.

In **Maximum — continuous** mode the next unified request is started immediately after the previous response is processed. No additional status transactions are inserted between live samples.

On hardware that reports roughly 119 ms round trip, the expected practical ceiling is around 7-8 updates/s. The exact value depends on the RD6012P firmware and USB/serial path.

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

If the displayed update rate remains low in Maximum mode, compare it with 1000 / RTT. For example, 119 ms RTT corresponds to an absolute transaction ceiling of about 8.4 Hz. A measured rate near 7-8 Hz is therefore already close to the device/link limit.

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
