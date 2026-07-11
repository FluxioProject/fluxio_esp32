#include "app_state.h"
#include <credentials.h>

String ssid = DEFAULT_WIFI_SSID;
String password = DEFAULT_WIFI_PASSWORD;

unsigned long serverStartTime = 0;
bool serverRunning = true;
uint8_t reconnectWiFi = 0;

String DEVICE_ID;

char MQTT_HOST[128] = "";
char MQTT_USER[64] = "";
char MQTT_PASS[64] = "";
int MQTT_PORT = 0;

bool telemetryEnabled = false;
unsigned long lastTelemetryCmd = 0;
char topicControl[64];
char topicTelemetry[64];
char topicOta[128];
char topicLogic[128];
char topicStatus[64];
String logicJsonCache;
bool manualMode = false;
bool apActive = false;
unsigned long apStartTime = 0;

void updateDeviceID() {
  uint64_t mac = ESP.getEfuseMac();
  char macStr[13];
  snprintf(macStr, sizeof(macStr), "%04X%08X", (uint16_t)(mac >> 32), (uint32_t)mac);
  DEVICE_ID = "esp32_" + String(macStr);
  Serial.println("\n\ndeviceID: " + DEVICE_ID);

  snprintf(topicTelemetry, sizeof(topicTelemetry), "device/%s/telemetry",
           DEVICE_ID.c_str());
}