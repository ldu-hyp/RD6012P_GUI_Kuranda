# RD6012P GUI Kuranda

Native C++ / Qt 6 desktop controller for the RIDEN RD6012P programmable power supply.

## Version

v0.2.2 — minimal V/I polling

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
- Live output voltage/current
- PC-side power calculation: `P = V * I`
- Live V / I / P graph
- Initial connection snapshot for input voltage, internal temperature, CV/CC, protection, preset and keypad lock
- Actual update-rate display
- Serial round-trip-time display
- User writes pre-empt background polling
- Timeout and one automatic retry

## v0.2.2 acquisition architecture

Real RD6012P V1.55 testing showed approximately:

- 8.4 Hz update rate
- 121 ms transaction round trip

This indicates that most latency is inside the device / USB-serial transaction path rather than caused by the amount of returned Modbus data.

v0.2.2 therefore minimizes the continuous live transaction as far as possible.

### Startup only

At connection time the application reads:

```
0x0004..0x0014
```

once to obtain:

- internal temperature
- V-SET / I-SET
- current range
- input voltage
- CV / CC
- protection
- output state
- preset
- keypad lock
- initial VOUT / IOUT

The current range is required because RD6012P current scaling differs between the 6 A and 12 A ranges.

### Continuous high-rate path

After startup, continuous acquisition reads only:

```
0x000A..0x000B
```

which contains:

- 0x000A — VOUT
- 0x000B — IOUT

Request size: 8 bytes.

Response size: 9 bytes.

Power is not read from the RD6012P. It is calculated locally:

```
P = VOUT * IOUT
```

This removes the power registers and all other status registers from the high-rate communication path.

### Write commands

Voltage, current, output and range writes still use Modbus function 0x06.

After an acknowledged write, the application updates its cached state from the echoed Modbus value and immediately resumes V/I acquisition. It does **not** insert an additional verification read, because one extra request costs roughly another full device transaction period.

## Important behavior in maximum-speed mode

The acquisition selector now shows:

```
Maximum — V/I only
```

In this mode:

- VOUT and IOUT are live
- power is calculated live on the PC
- set values changed through this GUI are kept in sync from write acknowledgements
- output/range changes made through this GUI are kept in sync from write acknowledgements
- temperature, input voltage, CV/CC, protection, preset and keypad-lock values are connection-time snapshots

If these latter values are changed from the RD6012P front panel while the GUI is connected, the GUI does not spend extra transactions refreshing them. This is intentional to preserve the maximum possible V/I sample rate.

## Expected speed improvement

At 115200 baud:

Previous unified transaction:

- request: 8 bytes
- response: 39 bytes
- total wire data: 47 bytes
- ideal wire time: about 4.1 ms

v0.2.2 V/I-only transaction:

- request: 8 bytes
- response: 9 bytes
- total wire data: 17 bytes
- ideal wire time: about 1.5 ms

So only about 2.6 ms of serial wire time is removed.

Because the measured total RTT is about 121 ms, the expected update-rate increase is modest. The important test is whether the RD6012P itself responds measurably faster to a two-register request.

## UI performance

The GUI thread never blocks on serial I/O.

QSerialPort and all Modbus scheduling run in a dedicated QThread.

Widgets are updated only when their displayed value changes, and set-point editors are not overwritten while the user is editing them.

The graph rendering cadence is independent from the instrument sampling cadence and renders at approximately 60 FPS. No fake measurement interpolation is used.

## Acquisition modes

- Maximum — V/I only: 0 ms host idle
- Fast: 20 ms host idle
- Balanced: 100 ms host idle
- Slow: 500 ms host idle

The **Actual update rate** and **Last round trip** fields show the real hardware/link performance.

## RD6012P scaling

- Voltage: 0.001 V/LSB
- Current in 6 A range: 0.0001 A/LSB
- Current in 12 A range: 0.001 A/LSB
- Input voltage: 0.01 V/LSB

## Build in Qt Creator

1. Install Qt 6.8.x MSVC 2022 64-bit.
2. Install the Qt Serial Port module.
3. Open the root `CMakeLists.txt` in Qt Creator.
4. Select the Qt 6.8 / MSVC 2022 64-bit kit.
5. Configure.
6. Build.
7. Run.

## Hardware-test note

This project is being tuned using real RD6012P V1.55 communication captures.

For first tests after an update, use conservative voltage/current limits and a non-critical load.
