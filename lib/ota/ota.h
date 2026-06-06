#pragma once

#include <Arduino.h>
#include <cstdint>

/** FreeRTOS queue that feeds OtaJob items to taskOTA. Created in main.cpp setup(). */
extern QueueHandle_t otaQueue;

/**
 * @brief Describes a pending OTA firmware update job.
 *
 * Populated by mqttCallback() when an OTA command is received, then
 * enqueued onto otaQueue for processing by taskOTA().
 */
typedef struct {
  char url[1024];  ///< HTTPS URL of the firmware binary.
  char sha256[65]; ///< Expected SHA-256 digest as a 64-character lowercase hex string.
  uint32_t size;   ///< Expected firmware size in bytes.
  char version[32]; ///< Human-readable version label (informational only).
} OtaJob;

/**
 * @brief Downloads, verifies, and applies a firmware update over HTTPS.
 *
 * Steps:
 *  1. Downloads the binary from job.url using a streaming GET request.
 *  2. Computes the SHA-256 digest of the received bytes using mbedTLS.
 *  3. Compares the digest against job.sha256; aborts if they differ.
 *  4. Calls ESP.restart() on success.
 *
 * @param job Update job parameters.
 * @return true if the update was applied successfully (device will restart),
 *         false on any error (HTTP, size mismatch, or SHA-256 mismatch).
 */
bool doOtaHttps(const OtaJob &job);

/**
 * @brief FreeRTOS task that waits for OtaJob items on otaQueue and runs doOtaHttps().
 *
 * Blocks indefinitely on the queue. Only one OTA job is processed at a time.
 *
 * @param pv Unused.
 */
void taskOTA(void *pv);
