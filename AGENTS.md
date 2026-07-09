# ESP32 Firmware — Agent Instructions

## Project Overview

ESP32 IoT firmware written in C++ with the Arduino framework via PlatformIO.
The device reads 4× analog input slots (currently 0-10 V) and 4× digital inputs, drives 4× analog (PWM) and 4× digital outputs,
runs a programmable logic engine, communicates over MQTT and HTTPS with a Firebase backend, and supports OTA updates.

## Build & Test

```bash
# Build
~/.platformio/penv/bin/pio run

# Windows build
& "$HOME\.platformio\penv\Scripts\pio.exe" run

# Build + flash (device must be connected)
~/.platformio/penv/bin/pio run --target upload

# Serial monitor
~/.platformio/penv/bin/pio device monitor
```

Enable simulated IO (no hardware required) by uncommenting `#define IO_SIMULATION` in `include/hw_config.h`.

## Architecture

```
src/main.cpp          — setup(), loop(), initTasks() only
include/
  hw_config.h         — compile-time hardware constants (pins, PWM, EEPROM layout, WDT)
  credentials.h       — gitignored secrets (WiFi, backend URL/token, TLS root CAs)
  credentials.example.h
lib/
  app_state/          — runtime globals: WiFi, MQTT state, topics, manualMode
  hal/                — HAL class: io arrays (ai/ao/di/doo), init(), updateIO()
  eeprom/             — EEPROMManager: read/write Wi-Fi creds + logic program
  webserver/          — MyWebServer: config portal + Wi-Fi connect + wifiandwdtTask
  mqtt/               — MQTT client, callbacks, telemetry, initMQTT()
  backend/            — HTTP calls: fetchMQTTCredentials, fetchAllChannels, setInitChannels
  alerts/             — Channel limit monitoring, DI/DO edge alerts, alertQueue, NVS offline persistence
  logic/              — Block-based logic engine (MATH/COMPARE/TIMER/IO blocks), NVS persistence
  ota/                — OTA via HTTPS with SHA-256 verification
  serial_test/        — Interactive debug console + unit test framework (real IO only)
```

## Key Conventions

- **All IO accessed through `hal` global** — `hal.ai[i]`, `hal.ao[i]`, `hal.di[i]`, `hal.doo[i]`
- **Mapping ranges** live on `hal.aiMapMin/Max[]`, `hal.aoMapMin/Max[]` — set by `fetchAllChannels()`
- **Alert metadata** (name, notify flags, trigger edge, lastState) lives in `lib/alerts/`
- **Alert NVS persistence** — failed HTTP POSTs are saved to NVS namespace `"alerts"` (up to 8 slots, FIFO eviction) and replayed every 30 s by `taskAlerts`
- **`IOType` enum** (`IO_AI/IO_DI/IO_AO/IO_DO`) lives in `logic.h` — use it instead of magic numbers 0–3 in `executeLogic()`
- **`#pragma once`** used in all headers — never `#ifndef` guards
- **C-style char arrays** for MQTT/EEPROM buffers; `String` only for Arduino API calls
- **FreeRTOS tasks** pinned to cores: MQTT + Telemetry + Logic → core 1; OTA + WDT + Alerts → core 0
- **TLS certificates** (`GCS_ROOT_CA`, `HIVEMQ_ROOT_CA`) are embedded in `credentials.h` as `PROGMEM` strings, but current MQTT/HTTPS clients use `setInsecure()` or have `setCACert(...)` commented out — see the **TLS Certificates** section below before production
- **Credentials never committed** — always edit `credentials.h` locally, refer to `credentials.example.h`
- **`IO_SIMULATION`** lets the full firmware compile and run without any hardware

## EEPROM Layout

Defined in `hw_config.h`:
- `SSID_ADDRESS = 0`, length 32
- `PASSWORD_ADDRESS = 32`, length 32
- Total size: 1024 bytes

## MQTT Topics

All topics are built in `initMQTT()` / `updateDeviceID()` using the pattern `device/<DEVICE_ID>/<type>`:
- `topicControl` — incoming commands (DO, AO, manual mode, telemetry toggle, logic program, OTA)
- `topicTelemetry` — outgoing JSON: `{"ai":[...],"ao":[...],"di":[...],"do":[...]}`
- `topicOta` — OTA job payloads
- `topicLogic` — logic program push / pull

## TLS Certificates

Root CA certificates are stored in `credentials.h`, but the current code does not enforce them:
`mqtt.cpp`, `backend.cpp`, and `alerts.cpp` use `setInsecure()`, and `ota.cpp` has `client.setCACert(GCS_ROOT_CA)` commented out.
Before production, replace those calls with `setCACert(...)` and keep the certificates current.

| Constant | Used by | CA | Expires | Renewal command |
|---|---|---|---|---|
| `HIVEMQ_ROOT_CA` | `mqtt.cpp` — use with `wifiClient.setCACert()` before production | Let's Encrypt R13 (ISRG Root X1) | **2027-03-12** | `openssl s_client -connect <broker>:8883 -showcerts` |
| `GCS_ROOT_CA` | `ota.cpp` — use with `client.setCACert()` before production | Google Trust Services WR2 | **2029-02-20** | `openssl s_client -connect storage.googleapis.com:443 -showcerts` |

### How to renew

```bash
# MQTT broker (HiveMQ) — copy the LAST cert block printed (root CA)
echo | openssl s_client -connect 48485b4717f24e64804b4902947d82d1.s1.eu.hivemq.cloud:8883 -showcerts 2>/dev/null \
  | awk '/BEGIN CERTIFICATE/{c++} c==2{print} /END CERTIFICATE/ && c==2{exit}'

# OTA download (Firebase Storage)
echo | openssl s_client -connect storage.googleapis.com:443 -showcerts 2>/dev/null \
  | awk '/BEGIN CERTIFICATE/{c++} c==2{print} /END CERTIFICATE/ && c==2{exit}'
```

Paste the output into the corresponding `R"EOF(...)EOF"` block in `credentials.h`.
Verify expiry: `echo | openssl s_client -connect <host>:<port> 2>/dev/null | openssl x509 -noout -dates`

## NVS Namespaces

| Namespace | Owner | Contents |
|---|---|---|
| `"logic"` | `lib/logic/` | Key `"program"` — last loaded logic program JSON |
| `"alerts"` | `lib/alerts/` | Keys `"a0".."a7"` + `"cnt"` — pending offline alerts |

## Do Not Change Without Understanding

- `alertQueue` must be created **before** `xTaskCreatePinnedToCore(taskAlerts, ...)` in `initTasks()`
- `esp_task_wdt_add(NULL)` must be the first call in `wifiandwdtTask` — the watchdog only monitors tasks that subscribe to it
- Logic block IDs from JSON must be `< MAX_BLOCKS (32)` — validated in `loadLogicFromJson()`
- `serverStartTime` is set inside `setupRoutes()` (when the server actually starts), not in `setup()`
