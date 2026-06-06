---
name: firmware-architecture
description: "Explains the ESP32 firmware architecture, module responsibilities, FreeRTOS task layout, IO data flow, logic engine format, and MQTT protocol. Use when: understanding how modules interact, tracing data flow from sensor to cloud, designing new features, or debugging task/memory issues."
---

# Firmware Architecture

## Module Map

| Module | File(s) | Responsibility |
|---|---|---|
| Hardware config | `include/hw_config.h` | Pins, PWM, ADC constants, EEPROM layout, WDT/WiFi timeouts |
| Credentials | `include/credentials.h` | Gitignored — WiFi, backend URL/token |
| App state | `lib/app_state/` | Runtime globals: DEVICE_ID, WiFi ssid/pass, MQTT host/port/topics, manualMode |
| HAL | `lib/hal/` | IO arrays (`ai/ao/di/doo`), mapping ranges, `init()`, `updateIO()` |
| EEPROM | `lib/eeprom/` | Read/write Wi-Fi credentials and logic program to EEPROM/NVS |
| Web server | `lib/webserver/` | Config portal (Wi-Fi form + local OTA), AP fallback, `wifiandwdtTask` |
| MQTT | `lib/mqtt/` | PubSubClient wrapper, `mqttCallback`, telemetry, `initMQTT` |
| Backend | `lib/backend/` | HTTP: fetch MQTT creds, fetch channel config, set defaults |
| Alerts | `lib/alerts/` | `ChannelLimit` structs, AI/AO limit checks, DI/DO edge detection, `alertQueue`, NVS offline persistence |
| Logic | `lib/logic/` | Block-based program engine, JSON parser, NVS persistence |
| OTA | `lib/ota/` | HTTPS download + SHA-256 verify + `Update` flash write |
| Serial test | `lib/serial_test/` | Interactive debug console + unit test framework (real IO only) |
| Entry point | `src/main.cpp` | `setup()`, `loop()`, `initTasks()` |

## IO Data Flow

```
Physical hardware
  └─ HAL::updateIO()
       ├─ ADC → mA → engineering units → hal.ai[i]
       ├─ digitalRead()                → hal.di[i]
       ├─ hal.doo[i]                   → digitalWrite()
       └─ hal.ao[i] → mA → PWM duty   → ledcWrite()

taskLogic (50 ms cycle)
  └─ hal.updateIO()
  └─ checkAiLimits / checkAoLimits / checkDiAlerts / checkDoAlerts → alertQueue
  └─ executeLogic() → reads hal.ai/di, writes hal.ao/doo → hal.updateIO()

taskMQTT
  └─ mqttCallback → writes hal.ao/doo directly, calls hal.updateIO()

taskTelemetry (1 Hz when enabled)
  └─ reads hal.ai/ao/di/doo → JSON → mqtt.publish(topicTelemetry)

taskAlerts
  └─ startup: replayPendingAlerts() from NVS
  └─ every 30 s: replayPendingAlerts()
  └─ drains alertQueue → HTTP POST /send-notification
       ├─ success → discard
       └─ failure → saveAlertToNVS() (up to 8 slots, FIFO eviction)
```

## FreeRTOS Tasks

| Task | Core | Priority | Stack | Purpose |
|---|---|---|---|---|
| `taskOTA` | 0 | 3 | 12 KB | HTTPS OTA download + flash |
| `wifiandwdtTask` | 0 | 2 | 4 KB | WDT reset + Wi-Fi reconnect every 15 s |
| `taskAlerts` | 0 | 1 | 12 KB | HTTPS alert POSTs (blocking OK, low priority) |
| `taskMQTT` | 1 | 2 | 8 KB | MQTT connect + loop + subscribe |
| `taskLogic` | 1 | 2 | 4 KB | IO update + alert check + logic engine @ 50 ms |
| `taskTelemetry` | 1 | 1 | 4 KB | 1 Hz telemetry while enabled |

**Rule:** MQTT and Logic share core 1. OTA, WDT, and Alerts share core 0 to isolate long-running HTTPS operations from time-sensitive tasks.

## Logic Engine

Programs are arrays of blocks. Each block reads from inputs (constants or other blocks' `lastValue`) and produces a single float output.

```
Block types:
  BLOCK_MATH    (0) — op: 0=add, 1=sub, 2=mul, 3=div
  BLOCK_COMPARE (1) — op: 0=>, 1=<, 2===, 3=>=, 4=<=  → output 0.0 or 1.0
  BLOCK_TIMER   (2) — on-delay: output→1 after input held high for inputs[1] ms
  BLOCK_IO      (3) — ioType: 0=AI read, 1=DI read, 2=AO write, 3=DO write

JSON format:
{
  "blocks": [
    { "id": 0, "t": 3, "io": [0, 0], "in": [] },          // read AI[0]
    { "id": 1, "t": 1, "op": 0, "in": [[1,0],[0,50.0]] }, // AI[0] > 50?
    { "id": 2, "t": 3, "io": [3, 0], "in": [[1,1]] }       // write DO[0] = block 1
  ]
}
```

Programs arrive via `topicControl` with `"type":"logic"` and are auto-persisted to NVS.

## TLS Certificates

Both connections use root CA certificates embedded as `PROGMEM` strings in `credentials.h`.

| Constant | Used by | CA | Expires |
|---|---|---|---|
| `HIVEMQ_ROOT_CA` | `mqtt.cpp` → `wifiClient.setCACert()` | Let's Encrypt R13 (ISRG Root X1) | 2027-03-12 |
| `GCS_ROOT_CA` | `ota.cpp` → `client.setCACert()` | Google Trust Services WR2 | 2029-02-20 |

To renew, re-extract with `openssl` and paste the new block into `credentials.h`:

```bash
# HiveMQ broker
echo | openssl s_client -connect 48485b4717f24e64804b4902947d82d1.s1.eu.hivemq.cloud:8883 -showcerts 2>/dev/null \
  | awk '/BEGIN CERTIFICATE/{c++} c==2{print} /END CERTIFICATE/ && c==2{exit}'

# Firebase Storage (OTA)
echo | openssl s_client -connect storage.googleapis.com:443 -showcerts 2>/dev/null \
  | awk '/BEGIN CERTIFICATE/{c++} c==2{print} /END CERTIFICATE/ && c==2{exit}'
```

## Alert Rate Limiting & Offline Persistence

Analog alerts fire at most once every **5 minutes** per channel (`300000 ms`).
Digital alerts fire on every edge that matches `diTrigger` / `doTrigger` (0=falling, 1=rising).
All alerts are decoupled from the logic loop via `alertQueue` (capacity 4 jobs).

When `sendAlertToBackend()` fails (Wi-Fi down or HTTP error ≥ 300), the `AlertJob` is
persisted to NVS namespace `"alerts"` as a compact JSON string. Up to **8 alerts** survive
reboots; the oldest entry is evicted when the queue is full. `replayPendingAlerts()` is called
at startup and every 30 s inside `taskAlerts`.

## Adding a New Feature

1. **New IO type** → update `hw_config.h` counts and add read/write to `hal.cpp::updateIO()`
2. **New MQTT command** → add a branch in `mqtt.cpp::mqttCallback()`
3. **New backend endpoint** → add function to `backend.cpp/h`
4. **New alert condition** → add check function in `alerts.cpp/h`, call from `taskLogic`
5. **New logic block type** → extend `BlockType` enum in `logic.h`, add case in `executeLogic()`
