#pragma once

#include <EEPROM.h>
#include <FS.h>
#include <HTTPClient.h>
#include <PubSubClient.h>
#include <Update.h>
#include <WebServer.h>
#include <app_state.h>
#include <credentials.h>
#include <string.h>

/**
 * @brief Manages the ESP32 HTTP configuration web server and Wi-Fi connection.
 *
 * Provides a captive-portal style interface for updating Wi-Fi credentials
 * and performing firmware upgrades over the local network.
 */
class MyWebServer {
public:
  MyWebServer() = default;

  /** @brief Starts the HTTP server and registers all route handlers. */
  void begin();

  /**
   * @brief Attempts to connect to the stored Wi-Fi network.
   *
   * Falls back to Access Point mode (SSID_ESP) after TIMEOUT_WIFI retries,
   * then calls setupRoutes() to start the web server regardless of outcome.
   */
  void connectWiFi();

  /** @brief Serves the device status / landing page. */
  void handleRoot();

  /** @brief Serves and handles the Wi-Fi credential update form (GET + POST). */
  void handleWiFiForm();

  /** @brief Serves the firmware upload page. */
  void handleFirmwareUpload();

  /** @brief Handles the multipart firmware binary upload and flashes it via Update. */
  void handleUploadFirmware();

  /** @brief Must be called in loop() to process pending HTTP requests. */
  void handleClient();

  /** @brief Stops the HTTP server. */
  void close();

private:
  /** @brief Creates a Wi-Fi Access Point using the credentials from credentials.h. */
  void createAP();

  /** @brief Registers all URL routes and starts the underlying WebServer. */
  void setupRoutes();

  /**
   * @brief Busy-waits for the given number of milliseconds.
   *        Used during Wi-Fi setup where vTaskDelay is not available yet.
   * @param delay Duration in milliseconds.
   */
  void myDelayMillis(uint16_t delay);

  WebServer server{80}; ///< Underlying ESP32 WebServer instance (port 80).
};

/** Global MyWebServer instance. */
extern MyWebServer webServer;

/**
 * @brief FreeRTOS task that monitors Wi-Fi connectivity and resets the WDT.
 *
 * Reconnects to Wi-Fi if the connection drops and periodically calls
 * esp_task_wdt_reset() to prevent the hardware watchdog from triggering.
 *
 * @param pvParameters Unused.
 */
void wifiandwdtTask(void *pvParameters);
