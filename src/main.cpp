#include "esp_task_wdt.h"
#include <WiFi.h>
#include <alerts.h>
#include <app_state.h>
#include <backend.h>
#include <eeprom_manager.h>
#include <hal.h>
#include <logic.h>
#include <mqtt.h>
#include <ota.h>
#include <serial_test.h>
#include <web_server.h>


void initTasks();

void setup() {
  WiFi.mode(WIFI_MODE_STA);
  Serial.begin(115200);
  updateDeviceID();

  hal.init();

  eepromManager.setupEEPROM();
  eepromManager.readParamsEEPROM();

  delay(3000);

  webServer.connectWiFi();

  if (!fetchMQTTCredentials() && WiFi.isConnected()) {
    Serial.println("Failed to fetch MQTT credentials, rebooting...");
    delay(3000);
    ESP.restart();
  }

  esp_task_wdt_init(WDT_TIMER, true);

  otaQueue = xQueueCreate(2, sizeof(OtaJob));

  setInitChannels();
  initMQTT();
  initTasks();

#ifdef IO_SIMULATION
  debugSerialInit();
#endif
}

void loop() {
#ifndef IO_SIMULATION
  debugSerialLoop();
#endif

  // 10 min to turn off web server
  if (serverRunning && (millis() - serverStartTime >= 600000)) {
    webServer.close();
    serverRunning = false;
    Serial.println("Server shut down after 10 minutes.");
  }

  webServer.handleClient();
}

void initTasks() {
  xTaskCreatePinnedToCore(taskOTA, "OTA", 12288, NULL, 3, NULL, 0);

  // Network tasks are always created — each task handles connectivity
  // internally, so they work correctly even when Wi-Fi is not connected at boot.
  alertQueue = xQueueCreate(4, sizeof(AlertJob));
  
  xTaskCreatePinnedToCore(taskMQTT, "MQTT", 8192, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(taskTelemetry, "Telemetry", 4096, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(taskAlerts, "Alerts",
                          12288, // safe stack size for HTTPS
                          NULL, 1, NULL, 0);

  xTaskCreatePinnedToCore(wifiandwdtTask, "Wifi_And_WDT", 4096, NULL, 2, NULL,
                          0);

  xTaskCreatePinnedToCore(taskLogic, "Logic", 4096, NULL, 2, NULL, 1);
}