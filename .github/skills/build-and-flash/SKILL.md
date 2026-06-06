---
name: build-and-flash
description: "Build, flash, and monitor the ESP32 firmware using PlatformIO. Use when: compiling the project, uploading firmware to the device, opening the serial monitor, running in simulation mode, or verifying the binary size."
argument-hint: "Optional: 'flash', 'monitor', or 'simulate'"
---

# Build and Flash — ESP32 Firmware

## When to Use

- Compile the firmware
- Upload to a connected ESP32-S3-DevKitM-1
- Open a serial monitor session
- Test without hardware using IO_SIMULATION
- Check RAM/Flash usage

## Commands

```bash
# Build only
~/.platformio/penv/bin/pio run

# Build and flash (device must be connected via USB)
~/.platformio/penv/bin/pio run --target upload

# Serial monitor (115200 baud)
~/.platformio/penv/bin/pio device monitor

# Clean build artifacts
~/.platformio/penv/bin/pio run --target clean
```

## Simulation Mode

1. Open `include/hw_config.h`
2. Uncomment `// #define IO_SIMULATION`
3. Build — no hardware needed. AI/DI channels return random values.

## Checking Binary Size

PlatformIO prints RAM and Flash usage at the end of every build:

```
RAM:   [==        ]  15.3% (used XXXXX bytes from 327680 bytes)
Flash: [===       ]  29.7% (used XXXXXX bytes from 3342336 bytes)
```

## Credentials Setup (first time)

```bash
cp include/credentials.example.h include/credentials.h
# Edit include/credentials.h — fill in SSID, password, backend URL/token
```

`credentials.h` is gitignored and must never be committed.

## Troubleshooting

| Symptom | Likely Cause | Fix |
|---|---|---|
| Upload fails | Wrong port or device not in flash mode | Hold BOOT button while pressing RESET |
| `credentials.h` not found | File not created | Run `cp` command above |
| Build succeeds but device reboots in loop | MQTT credentials fetch fails | Check Wi-Fi and backend connectivity |
| watchdog reset | `wifiandwdtTask` blocked | Verify task stack size and that `esp_task_wdt_add(NULL)` is called |
