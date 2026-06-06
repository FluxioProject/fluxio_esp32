#pragma once

#include <PubSubClient.h>
#include <WiFiClientSecure.h>

extern WiFiClientSecure wifiClient; ///< TLS-capable WiFi client used by the MQTT connection.
extern PubSubClient mqtt;           ///< Global MQTT client instance.

/**
 * @brief Initialises the MQTT client with broker credentials and builds
 *        the control, OTA, and logic topic strings.
 *
 * Must be called after updateDeviceID() and after MQTT credentials have
 * been fetched via fetchMQTTCredentials().
 */
void initMQTT();

/**
 * @brief Connects to the MQTT broker using the credentials from app_state.
 *
 * Skips if Wi-Fi is not connected or if the client is already connected.
 */
void connectMQTT();

/**
 * @brief MQTT message callback. Handles all incoming messages on subscribed topics.
 *
 * Processes: manual mode toggle, telemetry enable/disable, digital/analog
 * output commands, logic program updates, OTA jobs, and logic_get requests.
 *
 * @param topic   Topic the message was received on.
 * @param payload Raw message bytes.
 * @param length  Length of the payload in bytes.
 */
void mqttCallback(char *topic, byte *payload, unsigned int length);

/**
 * @brief Publishes a telemetry JSON payload with current AI, AO, DI, DO values.
 *
 * Payload format: `{"ai":[...],"ao":[...],"di":[...],"do":[...]}`.
 */
void sendTelemetry();

/**
 * @brief FreeRTOS task that maintains the MQTT connection and drives mqtt.loop().
 *
 * Calls connectMQTT() when disconnected, subscribes to control and OTA
 * topics after each reconnect, then calls mqtt.loop() every 10 ms.
 *
 * @param pvParameters Unused.
 */
void taskMQTT(void *pvParameters);

/**
 * @brief FreeRTOS task that publishes telemetry once per second while enabled.
 *
 * Automatically disables telemetry if no enable command has been received
 * within the last 10 seconds.
 *
 * @param pvParameters Unused.
 */
void taskTelemetry(void *pvParameters);