#pragma once

#include <Arduino.h>
#include <hw_config.h>

/** Unique device identifier derived from the ESP32 eFuse MAC address (e.g. "esp32_XXXXXXXXXXXX"). */
extern String DEVICE_ID;

// ----- Wi-Fi runtime state -----
extern String ssid;
extern String password;

extern unsigned long serverStartTime; ///< Timestamp (ms) when the config web server was started.
extern bool serverRunning;            ///< True while the config web server is active.
extern uint8_t reconnectWiFi;        ///< Retry counter used by the Wi-Fi watchdog task.

// ----- MQTT runtime state -----
extern char MQTT_HOST[128];
extern int MQTT_PORT;
extern char MQTT_USER[64];
extern char MQTT_PASS[64];
extern bool telemetryEnabled;         ///< True when periodic telemetry publishing is active.
extern unsigned long lastTelemetryCmd; ///< Timestamp (ms) of the last telemetry enable command.
extern char topicControl[64];         ///< MQTT topic for receiving control messages.
extern char topicTelemetry[64];       ///< MQTT topic for publishing telemetry payloads.
extern char topicOta[128];            ///< MQTT topic for receiving OTA update commands.
extern char topicLogic[128];          ///< MQTT topic for receiving / sending logic programs.
extern String logicJsonCache;         ///< Last-loaded logic program JSON, used for re-publishing.

extern bool manualMode; ///< When true, the logic engine is paused and outputs are set manually.

/**
 * @brief Derives DEVICE_ID from the ESP32 eFuse MAC address and builds all
 *        MQTT topic strings that depend on it.
 *
 * Must be called once in setup() before any network or MQTT code runs.
 */
void updateDeviceID();
