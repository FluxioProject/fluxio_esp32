#include "mqtt.h"
#include <WiFi.h>
#include <app_state.h>
#include <credentials.h>
#include <hal.h>
#include <logic.h>
#include <ota.h>


WiFiClientSecure wifiClient;
PubSubClient mqtt(wifiClient);

void connectMQTT() {
  if (WiFi.status() != WL_CONNECTED)
    return;
  if (mqtt.connected())
    return;

  Serial.print("MQTT...");

  char clientId[64];
  snprintf(clientId, sizeof(clientId), "%s", DEVICE_ID.c_str());

  if (mqtt.connect(clientId, MQTT_USER, MQTT_PASS)) {
    Serial.println(" connected");
  } else {
    Serial.print(" failed rc=");
    Serial.println(mqtt.state());
  }
}

void mqttCallback(char *topic, byte *payload, unsigned int length) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload, length);

  if (err) {
    Serial.println("Invalid MQTT JSON");
    return;
  }

  if (doc["manual"].is<bool>()) {
    manualMode = doc["manual"];
    Serial.printf("Manual mode: %s\n", manualMode ? "ON" : "OFF");
  }

  // ===== TELEMETRY =====
  if (doc["telemetry"].is<bool>()) {
    telemetryEnabled = doc["telemetry"];
    lastTelemetryCmd = millis();
    Serial.print("Telemetry ");
    Serial.println(telemetryEnabled ? "ON" : "OFF");
  }

  // ===== DIGITAL OUTPUT =====
  if (doc["do"].is<JsonObject>()) {
    int index = doc["do"]["index"] | -1;
    int value = doc["do"]["value"] | 0;

    if (index >= 0 && index < DO_COUNT) {
      hal.doo[index] = value ? 1 : 0;
      hal.updateIO();

      Serial.printf("DO[%d] = %d\n", index, hal.doo[index]);
    }
  }

  // ===== ANALOG OUTPUT =====
  if (doc["ao"].is<JsonObject>()) {
    int index = doc["ao"]["index"] | -1;
    float value = doc["ao"]["value"] | 0.0;

    if (index >= 0 && index < AO_COUNT) {
      hal.ao[index] = value;
      hal.updateIO();

      Serial.printf("AO[%d] = %.2f\n", index, hal.ao[index]);
    }
  }

  if (doc["type"] == "logic") {
    Serial.println("New logic program received via MQTT");
    logicJsonCache = String((const char *)payload, length);
    Serial.println(logicJsonCache);

    if (loadLogicFromJson(doc)) {
      saveLogicToFlash(logicJsonCache);
      Serial.println("Logic program updated and persisted");
    }
    return;
  }

  // ===== OTA =====
  const char *type = doc["type"] | "";
  if (strcmp(type, "ota") == 0) {
    const char *url = doc["url"] | "";
    const char *sha = doc["sha256"] | "";
    uint32_t size = doc["size"] | 0;
    const char *version = doc["version"] | "";

    if (!url[0] || strlen(sha) != 64 || size == 0) {
      Serial.println("Invalid OTA payload (url/sha/size)");
      return;
    }

    OtaJob job{};
    strncpy(job.url, url, sizeof(job.url) - 1);
    strncpy(job.sha256, sha, sizeof(job.sha256) - 1);
    job.size = size;
    strncpy(job.version, version, sizeof(job.version) - 1);

    if (otaQueue) {
      if (xQueueSend(otaQueue, &job, 0) != pdTRUE) {
        Serial.println("OTA queue full, ignoring OTA");
      } else {
        Serial.printf("OTA job queued version=%s size=%u\n", job.version,
                      job.size);
      }
    }
  }
  if (strcmp(type, "logic_get") == 0) {
    if (logicJsonCache.length() == 0) {
      Serial.println("No saved logic to send");
      return;
    }

    mqtt.publish(topicLogic, logicJsonCache.c_str(),
                 true // retain
    );

    Serial.println("Logic sent via MQTT");
    return;
  }
}

void taskMQTT(void *pvParameters) {
  static bool subscribed = false;

  for (;;) {
    if (!mqtt.connected()) {
      connectMQTT();
      subscribed = false;
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }

    if (!subscribed) {
      bool ok1 = mqtt.subscribe(topicControl);
      bool ok2 = mqtt.subscribe(topicOta);

      if (ok1 && ok2) {
        subscribed = true;
        Serial.println("Subscribed to control + ota topics");
      } else {
        Serial.println("Subscribe failed");
      }
    }

    mqtt.loop();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void sendTelemetry() {
  JsonDocument doc;

  JsonArray jai = doc["ai"].to<JsonArray>();
  JsonArray jao = doc["ao"].to<JsonArray>();
  JsonArray jdi = doc["di"].to<JsonArray>();
  JsonArray jdo = doc["do"].to<JsonArray>();

  for (int i = 0; i < AI_COUNT; i++) jai.add(hal.ai[i]);
  for (int i = 0; i < AO_COUNT; i++) jao.add(hal.ao[i]);
  for (int i = 0; i < DI_COUNT; i++) jdi.add(hal.di[i]);
  for (int i = 0; i < DO_COUNT; i++) jdo.add(hal.doo[i]);

  char payload[256];
  serializeJson(doc, payload);

  mqtt.publish(topicTelemetry, payload);
  Serial.println(payload);
}

void taskTelemetry(void *pvParameters) {
  for (;;) {
    if (!telemetryEnabled) {
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }

    if (millis() - lastTelemetryCmd < 10000) {
      sendTelemetry();
    } else {
      telemetryEnabled = false;
    }

    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void initMQTT() {
  // wifiClient.setCACert(HIVEMQ_ROOT_CA);
  wifiClient.setInsecure();
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(mqttCallback);

  snprintf(topicControl, sizeof(topicControl), "device/%s/control",
           DEVICE_ID.c_str());

  snprintf(topicOta, sizeof(topicOta), "device/%s/ota", DEVICE_ID.c_str());

  snprintf(topicLogic, sizeof(topicLogic), "device/%s/logic",
           DEVICE_ID.c_str());
}