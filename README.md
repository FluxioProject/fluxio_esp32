# Fluxio ESP32 Firmware

ESP32-S3 firmware for a multi-channel industrial I/O device with MQTT connectivity, a Firebase-backed HTTP API, OTA updates, alert delivery, and a local web configuration portal.

## Features

- 4 analog inputs for 4-20 mA signals mapped to configurable engineering units
- 4 analog outputs driven by PWM and external 4-20 mA conditioning circuitry
- 4 digital inputs with edge-triggered alerts
- 4 digital outputs controlled through MQTT
- JSON-based logic engine with math, compare, timer, and I/O blocks
- MQTT over TLS using an embedded root CA certificate
- HTTPS OTA updates with TLS verification and SHA-256 integrity checks
- Local web portal for Wi-Fi configuration and firmware upload
- Alert queue with retry persistence in non-volatile storage
- FreeRTOS task split for MQTT, telemetry, OTA, alerts, watchdog, and logic execution

## Hardware

| Component | Detail |
|---|---|
| MCU | ESP32-S3-DevKitM-1 |
| AI 0-3 | 4-20 mA inputs on pins 4, 5, 6, 7 using ADC1, 12-bit resolution, and 150 ohm shunts |
| AO 0-3 | PWM outputs on pins 21, 38, 39, 40 using LEDC at 5 kHz and 8-bit resolution |
| DI 0-3 | Digital inputs on pins 17, 18, 14, 15 with `INPUT_PULLDOWN` |
| DO 0-3 | Digital outputs on pins 16, 13, 12, 11 |

The ESP32-S3 does not include DAC outputs. Analog outputs use PWM, so the hardware must include the required filtering and current-driver stage for a real 4-20 mA output.

Pin assignments and hardware constants are defined in `include/hw_config.h`. Set a channel pin to `-1` to disable it.

## Requirements

- PlatformIO CLI or the VS Code PlatformIO extension
- ESP32-S3-DevKitM-1 or compatible hardware
- A Fluxio backend endpoint
- MQTT broker credentials returned by the backend

## Configuration

Copy the credential template:

```bash
cp include/credentials.example.h include/credentials.h
```

Edit `include/credentials.h` with:

- Wi-Fi fallback access point credentials
- Default Wi-Fi station credentials
- Fluxio backend URL
- Shared backend API token
- MQTT root CA certificate
- OTA download root CA certificate

`include/credentials.h` is ignored by Git and must not be committed.

## Build and Flash

```bash
pio run
pio run --target upload
pio device monitor
```

The serial monitor runs at `115200` baud.

## Simulated I/O

To build and test without physical I/O hardware, uncomment `#define IO_SIMULATION` in `include/hw_config.h`. In simulation mode, analog and digital inputs are generated in software during `updateIO()`.

## Architecture

On boot, the firmware:

1. Initializes hardware and persistent state
2. Connects to Wi-Fi, falling back to AP mode when station connection fails
3. Fetches MQTT credentials from the backend
4. Fetches channel configuration
5. Starts MQTT, telemetry, alert, OTA, watchdog, and logic tasks

Main runtime modules:

- `lib/hal/` - hardware abstraction and I/O updates
- `lib/eeprom/` - persistent parameter storage
- `lib/webserver/` - Wi-Fi and local configuration portal
- `lib/backend/` - HTTP requests to the Fluxio backend
- `lib/mqtt/` - MQTT connection, subscriptions, and telemetry publishing
- `lib/alerts/` - channel limit detection and alert queue processing
- `lib/logic/` - block-based automation engine
- `lib/ota/` - HTTPS firmware download and validation
- `lib/serial_test/` - serial debug console and tests

## MQTT Control Protocol

Commands are published to `device/<DEVICE_ID>/control`.

| Payload | Action |
|---|---|
| `{"manual": true}` or `{"manual": false}` | Pause or resume the logic engine |
| `{"telemetry": true}` or `{"telemetry": false}` | Start or stop telemetry publishing |
| `{"do": {"index": 0, "value": 1}}` | Set a digital output |
| `{"ao": {"index": 0, "value": 50.0}}` | Set an analog output engineering value |
| `{"type": "logic", "blocks": [...]}` | Load and persist a logic program |
| `{"type": "logic_get"}` | Republish the saved logic program |
| `{"type": "ota", "url": "...", "sha256": "...", "size": 123}` | Trigger an OTA update |

## Logic Programs

Logic programs are JSON objects with a `blocks` array. Each block includes:

- `id` - unique block identifier
- `t` - block type: `0` math, `1` compare, `2` timer, `3` I/O
- `op` - operation code for the selected block type
- `io` - I/O selector for I/O blocks
- `in` - input references or constants

Example: if AI0 is greater than 75.0, set DO0 to 1.

```jsonc
{
  // Root message type: tells the firmware this payload is a logic program.
  "type": "logic",

  // Ordered list of blocks that form the program.
  "blocks": [
    { "id": 10, "t": 3, "op": 0, "io": [0, 0] },
    { "id": 0, "t": 1, "op": 1, "in": [[1, 10], [0, 75.0]] },
    { "id": 1, "t": 3, "op": 1, "io": [3, 0], "in": [[1, 0]] }
  ]
}
```

## Serial Debug Console

Available commands:

```text
test            run all unit tests
print           print current IO state
do <idx> <0|1>  set digital output
ao <idx> <val>  set analog output
di <idx> <0|1>  override digital input
ai <idx> <val>  override analog input
help            show this menu
```

The console is only available when `IO_SIMULATION` is not defined.

## Security Notes

- Keep `include/credentials.h` out of source control.
- Rotate the shared backend API token if it is exposed.
- Keep TLS root CA certificates current for the MQTT broker and OTA download host.
- OTA updates are accepted only when both TLS verification and SHA-256 validation succeed.

## License

Add the project license before distributing firmware or source releases.
