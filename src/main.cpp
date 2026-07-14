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
void taskBootFirmwareCheck(void *pv)
{
  const uint32_t timeoutMs = 25000;
  uint32_t start = millis();

  while (!mqtt.connected() && (millis() - start) < timeoutMs)
  {
    vTaskDelay(pdMS_TO_TICKS(200));
  }

  vTaskDelay(pdMS_TO_TICKS(1000));

  if (!checkForFirmwareUpdate())
  {
    Serial.println("[FW] First check failed, retrying once in 5s...");
    vTaskDelay(pdMS_TO_TICKS(5000));
    checkForFirmwareUpdate();
  }

  vTaskDelete(NULL);
}

void taskBootLogicSync(void *pv)
{
  const uint32_t timeoutMs = 25000;
  uint32_t start = millis();

  while (!mqtt.connected() && (millis() - start) < timeoutMs)
  {
    vTaskDelay(pdMS_TO_TICKS(200));
  }

  vTaskDelay(pdMS_TO_TICKS(1000));

  syncLogicFromBackend();

  vTaskDelete(NULL);
}

void setup()
{
  WiFi.mode(WIFI_MODE_STA);
  Serial.begin(115200);
  updateDeviceID();

  hal.init();

  eepromManager.setupEEPROM();
  eepromManager.readParamsEEPROM();

  initHttpsMutex();

  delay(3000);

  webServer.connectWiFi();

  delay(3000);

  if (!fetchMQTTCredentials() && WiFi.isConnected())
  {
    Serial.println("Failed to fetch MQTT credentials, rebooting...");
    delay(1000);
    ESP.restart();
  }

  esp_task_wdt_init(WDT_TIMER, true);

  otaQueue = xQueueCreate(2, sizeof(OtaJob));
  alertQueue = xQueueCreate(4, sizeof(AlertJob));

  loadFirmwareVersion();

  initMQTT();
  initTasks();

  setInitChannels();

#ifndef IO_SIMULATION
  debugSerialInit();
#endif
}

void loop()
{
#ifndef IO_SIMULATION
  // debugSerialLoop();
#endif

  // 10 min to turn off web server
  if (serverRunning && (millis() - serverStartTime >= 600000))
  {
    webServer.close();
    serverRunning = false;
    Serial.println("Server shut down after 10 minutes.");
  }

  webServer.handleClient();
}

void initTasks()
{
  xTaskCreatePinnedToCore(taskOTA, "OTA", 12288, NULL, 3, NULL, 0);

  // Network tasks are always created. Each task handles connectivity
  // internally and can recover if Wi-Fi is temporarily unavailable.
  xTaskCreatePinnedToCore(taskMQTT, "MQTT", 8192, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(taskTelemetry, "Telemetry", 4096, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(taskAlerts, "Alerts", 8192, NULL, 1, NULL, 0);

  xTaskCreatePinnedToCore(wifiandwdtTask, "Wifi_And_WDT", 4096, NULL, 2, NULL,
                          0);

  xTaskCreatePinnedToCore(taskLogic, "Logic", 12288, NULL, 2, NULL, 1);

  xTaskCreatePinnedToCore(taskBootFirmwareCheck, "FWCheck", 12288, NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore(taskBootLogicSync, "BootLogicSync", 12288, NULL, 1, NULL, 0);
}