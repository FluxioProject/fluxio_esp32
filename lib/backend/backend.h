#pragma once

#include "app_state.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <alerts.h>

extern SemaphoreHandle_t httpsMutex;
extern WiFiClientSecure sharedHttpsClient;

void initHttpsMutex();

/**
 * @brief Fetches MQTT broker credentials from the backend and stores them in
 *        MQTT_HOST, MQTT_PORT, MQTT_USER, and MQTT_PASS.
 *
 * Endpoint: GET /mqtt?deviceId=<DEVICE_ID>
 *
 * @return true on success, false if Wi-Fi is unavailable or the request fails.
 */
bool fetchMQTTCredentials();

/**
 * @brief Downloads per-channel configuration (name, mapping range, alert limits,
 *        notification flags) for all AI, AO, DI, and DO channels from the backend
 *        and populates the corresponding HAL and alerts arrays.
 *
 * Endpoint: GET /get-all-channels?deviceId=<DEVICE_ID>
 *
 * @return true on success, false on HTTP or JSON error.
 */
bool fetchAllChannels();

/**
 * @brief Resets all channel metadata to safe defaults, then calls fetchAllChannels()
 *        to overwrite them with backend values.
 *
 * Should be called once in setup() after Wi-Fi is connected. Prints a warning
 * to Serial if the backend fetch fails but does not abort.
 */
void setInitChannels();

/**
 * @brief Checks the backend for the latest firmware version for this device,
 *        and enqueues an OTA job if it differs from currentFwVersion.
 *
 * Endpoint: GET /devices/<DEVICE_ID>/firmware/latest
 *
 * @return true if an OTA job was successfully queued, false otherwise.
 */
bool checkForFirmwareUpdate();

/**
 * @brief Fetches the logic program for this device from the backend.
 *
 * Endpoint: GET /products/<DEVICE_ID>/logic-for-device
 *
 * @param[out] outJson The JSON string of the logic program, if successful.
 * @param[out] outUpdatedAt The "updatedAt" timestamp from the backend, if successful.
 * @return true on success, false on HTTP or JSON error.
 */
bool fetchLogicFromBackend(String &outJson, String &outUpdatedAt);