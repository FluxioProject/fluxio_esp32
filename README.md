# TCC ESP32 — IoT Firmware

ESP32-S3 firmware for a multi-channel industrial I/O device with MQTT connectivity, a cloud backend, OTA updates, and a web-based configuration interface.

## Features

- **4× Analog Input (AI)** — 4–20 mA signals mapped to configurable engineering units
- **4× Analog Output (AO)** — PWM-driven 4–20 mA output
- **4× Digital Input (DI)** — edge-triggered alerts
- **4× Digital Output (DO)** — remotely controlled via MQTT
- **Programmable Logic Engine** — JSON-defined block programs (MATH, COMPARE, TIMER, IO) pushed via MQTT, persisted to NVS flash
- **MQTT over TLS** — verified against embedded Let's Encrypt root CA (`HIVEMQ_ROOT_CA` in `credentials.h`)
- **OTA Updates** — HTTPS download verified against embedded Google Trust Services root CA (`GCS_ROOT_CA`) + SHA-256 integrity check
- **Web Configuration Portal** — Wi-Fi credential update and local firmware upload via browser
- **Alert System** — out-of-range notifications for AI/AO channels, edge notifications for DI/DO, delivered to a Firebase backend; failed POSTs are persisted to NVS and retried automatically
- **FreeRTOS** — multi-core task architecture (logic + MQTT on core 1; OTA + alerts + WDT on core 0)

## Hardware

| Component | Detail |
|---|---|
| MCU | ESP32-S3-DevKitM-1 |
| AI [0–3] | 4–20 mA on pins 4, 5, 6, 7 — ADC1 channels, 12-bit, 150 Ω shunt |
| AO [0–3] | PWM (LEDC) on pins 21, 38, 39, 40 — 5 kHz, 8-bit, 150 Ω shunt → 4–20 mA |
| DI [0–3] | Digital inputs on pins 17, 18, 14, 15 (INPUT_PULLDOWN) |
| DO [0–3] | Digital outputs on pins 16, 13, 12, 11 |

> **Note:** The ESP32-S3 has no DAC. Analog outputs use PWM via the LEDC peripheral; an external RC filter and current driver circuit are required to produce the 4–20 mA signal. Pin assignments are arrays in `hw_config.h` — set any entry to `-1` to disable that channel.
>
> AO conversion chain (example: 50 bar, scale 0–100 bar):
> ```
> eng  = 50 bar
>   → imA  = map(50, 0, 100, 4, 20)      = 12 mA
>   → vout = 0.012 A × 150 Ω             = 1.8 V    (V = I × R)
>   → duty = (1.8 / 3.3) × 255           ≈ 139      (8-bit PWM)
>   → ledcWrite(ch, 139)
> ``` 

## Getting Started

### Prerequisites

- [PlatformIO](https://platformio.org/) CLI or the VS Code PlatformIO extension

### 1. Clone

```bash
git clone <repo-url>
cd tcc_esp32
```

### 2. Create credentials

```bash
cp include/credentials.example.h include/credentials.h
```

Edit `include/credentials.h` and fill in your Wi-Fi fallback credentials and Firebase backend URL/token.

### 3. Build

```bash
~/.platformio/penv/bin/pio run
```

### 4. Flash

```bash
~/.platformio/penv/bin/pio run --target upload
```

### 5. Monitor

```bash
~/.platformio/penv/bin/pio device monitor
```

## Simulated IO

To build and test without hardware, uncomment `#define IO_SIMULATION` in `include/hw_config.h`.
In this mode, analog inputs and digital inputs are filled with random values on each `updateIO()` call.

## Project Structure

```
tcc_esp32/
├── include/
│   ├── hw_config.h             # Hardware constants (pins, PWM, EEPROM, WDT)
│   ├── credentials.h           # Gitignored secrets
│   └── credentials.example.h  # Template for credentials
├── src/
│   └── main.cpp                # setup(), loop(), initTasks()
├── lib/
│   ├── app_state/              # Runtime globals (Wi-Fi, MQTT state, topics)
│   ├── hal/                    # Hardware Abstraction Layer (IO arrays, updateIO)
│   ├── eeprom/                 # EEPROM read/write wrapper
│   ├── webserver/              # Config web server, Wi-Fi connect, WDT task
│   ├── mqtt/                   # MQTT client, callbacks, telemetry
│   ├── backend/                # HTTP calls to Firebase backend
│   ├── alerts/                 # Channel limit monitoring, alert queue
│   ├── logic/                  # Block-based logic engine, NVS persistence
│   ├── ota/                    # HTTPS OTA with SHA-256 verification
│   └── serial_test/            # Interactive debug console + unit tests
├── platformio.ini
└── AGENTS.md
```

## Architecture

The firmware boots in `setup()` which:
1. Connects to Wi-Fi (falls back to AP mode after `TIMEOUT_WIFI` retries)
2. Fetches MQTT credentials from the backend
3. Fetches channel configuration (mapping ranges, alert limits, names)
4. Starts all FreeRTOS tasks

```
setup()
  └─ webServer.connectWiFi()       → connect / AP fallback
  └─ fetchMQTTCredentials()        → GET /mqtt
  └─ setInitChannels()             → defaults + GET /get-all-channels
  └─ initMQTT()                    → configure PubSubClient
  └─ initTasks()
       ├─ taskOTA          (core 0, priority 3) → waits on otaQueue
       ├─ taskMQTT         (core 1, priority 2) → connects, loops
       ├─ taskTelemetry    (core 1, priority 1) → publishes 1 Hz when enabled
       ├─ taskAlerts       (core 0, priority 1) → drains alertQueue, HTTP POST
       ├─ wifiandwdtTask   (core 0, priority 2) → WDT reset + reconnect
       └─ taskLogic        (core 1, priority 2) → updateIO + alerts + logic @ 50 ms

loop()
  └─ debugSerialLoop()             → interactive console + periodic IO dump
  └─ webServer.handleClient()      → HTTP server (auto-closes after 10 min)
```

### Logic Engine

Logic programs are JSON objects with a `"blocks"` array. Each block has:
- `id` — unique identifier (0–31)
- `t` — block type: `0`=MATH, `1`=COMPARE, `2`=TIMER, `3`=IO
- `op` — operation code (type-specific)
- `io` — `[ioType, channel]` for IO blocks (`0`=AI, `1`=DI, `2`=AO, `3`=DO)
- `in` — input array, each entry `[kind, value]` where `kind=0` is a constant and `kind=1` references another block by ID

Programs are pushed via MQTT on `topicControl` and persisted to NVS flash automatically.

**Example:** If AI[0] > 75.0, set DO[0] = 1

```json
{
  "type": "logic",
  "blocks": [
    {
      "id": 0,
      "t": 1,
      "op": 1,
      "in": [
        [1, 10],
        [0, 75.0]
      ]
    },
    {
      "id": 10,
      "t": 3,
      "op": 0,
      "io": [0, 0]
    },
    {
      "id": 1,
      "t": 3,
      "op": 1,
      "io": [3, 0],
      "in": [
        [1, 0]
      ]
    }
  ]
}
```

> Block 10 reads AI[0] (`"io": [0, 0]`). Block 0 compares it (`op=1` → `>`) against constant 75.0. Block 1 writes the result to DO[0] (`"io": [3, 0]`).

## MQTT Control Protocol

Send JSON to `device/<DEVICE_ID>/control`:

| Field | Type | Action |
|---|---|---|
| `"manual": true/false` | bool | Pause / resume logic engine |
| `"telemetry": true/false` | bool | Start / stop 1 Hz telemetry for 10 s |
| `"do": {"index": N, "value": 0/1}` | object | Set digital output N |
| `"ao": {"index": N, "value": F}` | object | Set analog output N |
| `"type": "logic"` + blocks | object | Load and persist a logic program |
| `"type": "logic_get"` | object | Re-publish saved logic on `topicLogic` |
| `"type": "ota"` + url/sha256/size | object | Trigger OTA update |

## Debug Console

Connect a serial terminal at 115200 baud. Available commands:

```
test            run all unit tests
print           print current IO state
do <idx> <0|1>  set digital output
ao <idx> <val>  set analog output
di <idx> <0|1>  override digital input
ai <idx> <val>  override analog input
help            show this menu
```

The console is only available when `IO_SIMULATION` is **not** defined.

## Security

### TLS Certificates

Both TLS connections verify the server identity using root CA certificates embedded in `credentials.h` as `PROGMEM` strings.

| Connection | Constant | CA | Expires |
|---|---|---|---|
| MQTT (HiveMQ Cloud, port 8883) | `HIVEMQ_ROOT_CA` | Let's Encrypt R13 / ISRG Root X1 | 2027-03-12 |
| OTA download (Firebase Storage) | `GCS_ROOT_CA` | Google Trust Services WR2 | 2029-02-20 |

### Renewing a certificate

When a cert is close to expiry, re-extract it and replace the block in `credentials.h`:

```bash
# MQTT broker (HiveMQ)
echo | openssl s_client -connect 48485b4717f24e64804b4902947d82d1.s1.eu.hivemq.cloud:8883 -showcerts 2>/dev/null \
  | awk '/BEGIN CERTIFICATE/{c++} c==2{print} /END CERTIFICATE/ && c==2{exit}'

# OTA / Firebase Storage
echo | openssl s_client -connect storage.googleapis.com:443 -showcerts 2>/dev/null \
  | awk '/BEGIN CERTIFICATE/{c++} c==2{print} /END CERTIFICATE/ && c==2{exit}'
```

Verify expiry before and after:
```bash
echo | openssl s_client -connect <host>:<port> 2>/dev/null | openssl x509 -noout -dates
```

### OTA integrity

Every OTA update is protected by two independent mechanisms:
1. **TLS CA verification** — the download connection is verified against `GCS_ROOT_CA`; a MITM cannot substitute a different server
2. **SHA-256 checksum** — computed over the downloaded bytes and compared against the hash published via MQTT; a corrupted or tampered binary is rejected before flashing

## Configuration

All hardware constants are in `include/hw_config.h`:

| Constant | Default | Description |
|---|---|---|
| `AI_COUNT` / `AO_COUNT` / `DI_COUNT` / `DO_COUNT` | 4 | Channel counts |
| `WDT_TIMER` | 10 s | Hardware watchdog timeout |
| `TIMEOUT_WIFI` | 18 | Wi-Fi connect retries before AP fallback |
| `EEPROM_SIZE` | 1024 | Total EEPROM bytes allocated |
| `PWM_FREQ` / `PWM_RES` | 5000 Hz / 8-bit | AO PWM settings |
| `SHUNT_OHMS` | 150 Ω | Current sense resistor for AI/AO |
| `VREF` | 3.3 V | ADC reference voltage |

## License

Academic project — TCC (Trabalho de Conclusão de Curso).
