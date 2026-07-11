#include "mqtt.h"
#include <WiFi.h>
#include <app_state.h>
#include <credentials.h>
#include <hal.h>
#include <logic.h>
#include <ota.h>

WiFiClientSecure wifiClient;
PubSubClient mqtt(wifiClient);

static const uint16_t MQTT_BUFFER_SIZE = 1024;
static uint32_t mqttReconnectAttempts = 0;
static uint32_t lastConnectAttemptMs = 0;
static uint32_t lastStatusPublishMs = 0;
static const uint32_t STATUS_HEARTBEAT_MS = 2000;

const char *mqttStateStr(int state)
{
  switch (state)
  {
  case -4:
    return "MQTT_CONNECTION_TIMEOUT";
  case -3:
    return "MQTT_CONNECTION_LOST";
  case -2:
    return "MQTT_CONNECT_FAILED (TCP/TLS failed)";
  case -1:
    return "MQTT_DISCONNECTED";
  case 0:
    return "MQTT_CONNECTED";
  case 1:
    return "MQTT_CONNECT_BAD_PROTOCOL";
  case 2:
    return "MQTT_CONNECT_BAD_CLIENT_ID";
  case 3:
    return "MQTT_CONNECT_UNAVAILABLE (broker refused)";
  case 4:
    return "MQTT_CONNECT_BAD_CREDENTIALS";
  case 5:
    return "MQTT_CONNECT_UNAUTHORIZED";
  default:
    return "UNKNOWN";
  }
}

static void buildOnlineStatusPayload(char *buf, size_t bufSize)
{
  String ip = WiFi.isConnected() ? WiFi.localIP().toString() : "";
  uint32_t uptimeSec = millis() / 1000;

  snprintf(buf, bufSize,
           "{\"online\":true,\"ip\":\"%s\",\"uptime\":%lu}",
           ip.c_str(), uptimeSec);
}

static void publishOnlineStatus()
{
  char statusPayload[96];
  buildOnlineStatusPayload(statusPayload, sizeof(statusPayload));
  bool sent = mqtt.publish(topicStatus, statusPayload, true); // retained
  Serial.printf("[MQTT] Status published %s: %s\n",
                sent ? "OK" : "FAILED", statusPayload);
}

void connectMQTT()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("[MQTT] Skipped connect: Wi-Fi not connected");
    return;
  }
  if (mqtt.connected())
    return;

  uint32_t now = millis();
  Serial.printf("[MQTT] Connecting to %s:%d (attempt #%lu, RSSI=%d dBm, freeHeap=%u)...\n",
                MQTT_HOST, MQTT_PORT, ++mqttReconnectAttempts, WiFi.RSSI(),
                ESP.getFreeHeap());

  char clientId[64];
  snprintf(clientId, sizeof(clientId), "%s", DEVICE_ID.c_str());

  // Last Will: if the connection drops uncleanly, the broker publishes this
  // retained "offline" message on our behalf.
  const char *willMessage = "{\"online\":false}";

  uint32_t t = millis();

  bool ok = mqtt.connect(clientId, MQTT_USER, MQTT_PASS,
                         topicStatus, 1, true, willMessage);

  Serial.printf("DEPOIS connect() (%lu ms)\n", millis() - t);

  uint32_t elapsed = millis() - now;

  if (ok)
  {
    Serial.printf("[MQTT] Connected in %lu ms\n", elapsed);
    mqttReconnectAttempts = 0;

    char statusPayload[96];
    buildOnlineStatusPayload(statusPayload, sizeof(statusPayload));

    bool sent = mqtt.publish(topicStatus, statusPayload, true); // retained
    Serial.printf("[MQTT] Status published %s: %s\n",
                  sent ? "OK" : "FAILED", statusPayload);
  }
  else
  {
    Serial.printf("[MQTT] Connect failed in %lu ms, state=%d (%s)\n", elapsed,
                  mqtt.state(), mqttStateStr(mqtt.state()));
  }

  lastConnectAttemptMs = now;
}

void mqttCallback(char *topic, byte *payload, unsigned int length)
{
  Serial.printf("[MQTT] RX topic=%s len=%u\n", topic, length);

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload, length);

  if (err)
  {
    Serial.printf("[MQTT] Invalid JSON on topic %s: %s\n", topic, err.c_str());
    return;
  }

  if (doc["manual"].is<bool>())
  {
    manualMode = doc["manual"];
    Serial.printf("[MQTT] Manual mode: %s\n", manualMode ? "ON" : "OFF");
  }

  if (doc["telemetry"].is<bool>())
  {
    telemetryEnabled = doc["telemetry"];
    lastTelemetryCmd = millis();
    Serial.printf("[MQTT] Telemetry: %s\n", telemetryEnabled ? "ON" : "OFF");
  }

  if (doc["do"].is<JsonObject>())
  {
    int index = doc["do"]["index"] | -1;
    int value = doc["do"]["value"] | 0;

    if (index >= 0 && index < DO_COUNT)
    {
      hal.doo[index] = value ? 1 : 0;
      hal.updateIO();
      Serial.printf("[MQTT] DO[%d] = %d\n", index, hal.doo[index]);
    }
    else
    {
      Serial.printf("[MQTT] DO command with invalid index: %d\n", index);
    }
  }

  if (doc["ao"].is<JsonObject>())
  {
    int index = doc["ao"]["index"] | -1;
    float value = doc["ao"]["value"] | 0.0;

    if (index >= 0 && index < AO_COUNT)
    {
      hal.ao[index] = value;
      hal.updateIO();
      Serial.printf("[MQTT] AO[%d] = %.2f\n", index, hal.ao[index]);
    }
    else
    {
      Serial.printf("[MQTT] AO command with invalid index: %d\n", index);
    }
  }

  if (doc["type"] == "logic")
  {
    Serial.println("================ LOGIC RECEIVED ================");
    Serial.println((const char *)payload);
    Serial.println("=================================================");

    Serial.println("[MQTT] New logic program received");

    logicJsonCache = String((const char *)payload, length);

    if (loadLogicFromJson(doc))
    {
      saveLogicToFlash(logicJsonCache);
      Serial.println("[MQTT] Logic program updated and persisted");
    }
    else
    {
      Serial.println("[MQTT] Failed to load logic program from JSON");
    }
    return;
  }

  const char *type = doc["type"] | "";
  if (strcmp(type, "ota") == 0)
  {
    const char *url = doc["url"] | "";
    const char *sha = doc["sha256"] | "";
    uint32_t size = doc["size"] | 0;
    const char *version = doc["version"] | "";

    if (!url[0] || strlen(sha) != 64 || size == 0)
    {
      Serial.println("[MQTT] Invalid OTA payload (missing url/sha/size)");
      return;
    }

    OtaJob job{};
    strncpy(job.url, url, sizeof(job.url) - 1);
    strncpy(job.sha256, sha, sizeof(job.sha256) - 1);
    job.size = size;
    strncpy(job.version, version, sizeof(job.version) - 1);

    if (otaQueue)
    {
      if (xQueueSend(otaQueue, &job, 0) != pdTRUE)
      {
        Serial.println("[MQTT] OTA queue full, job dropped");
      }
      else
      {
        Serial.printf("[MQTT] OTA job queued version=%s size=%u\n", job.version,
                      job.size);
      }
    }
    return;
  }

  if (strcmp(type, "logic_get") == 0)
  {
    if (logicJsonCache.length() == 0)
    {
      Serial.println("[MQTT] logic_get requested but no cached logic to send");
      return;
    }
    bool sent = mqtt.publish(topicLogic, logicJsonCache.c_str(), true);

    Serial.printf("[MQTT] logic_get response %s (%u bytes)\n",
                  sent ? "sent" : "FAILED",
                  logicJsonCache.length());

    Serial.println("================ LOGIC SENT ================");
    Serial.println(logicJsonCache);
    Serial.println("============================================");
  }
}

void taskMQTT(void *pvParameters)
{
  static bool subscribed = false;
  static uint32_t lastLoopLog = 0;

  for (;;)
  {
    if (apActive)
    {
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }

    if (!mqtt.connected())
    {
      if (subscribed)
      {
        Serial.printf("[MQTT] Connection lost, state=%d (%s)\n", mqtt.state(),
                      mqttStateStr(mqtt.state()));
      }
      connectMQTT();
      subscribed = false;
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }

    if (!subscribed)
    {
      bool ok1 = mqtt.subscribe(topicControl);
      bool ok2 = mqtt.subscribe(topicOta);

      if (ok1 && ok2)
      {
        subscribed = true;
        Serial.printf("[MQTT] Subscribed to %s and %s\n", topicControl, topicOta);
      }
      else
      {
        Serial.printf("[MQTT] Subscribe failed (control=%d, ota=%d), retrying\n",
                      ok1, ok2);
      }
    }

    mqtt.loop();

    uint32_t now = millis();
    if (mqtt.connected() && now - lastStatusPublishMs >= STATUS_HEARTBEAT_MS)
    {
      lastStatusPublishMs = now;
      publishOnlineStatus();
    }

    if (now - lastLoopLog >= 30000)
    {
      lastLoopLog = now;
      Serial.printf("[MQTT] Alive, connected=%d, RSSI=%d dBm, freeHeap=%u\n",
                    mqtt.connected(), WiFi.RSSI(), ESP.getFreeHeap());
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void sendTelemetry()
{
  JsonDocument doc;

  JsonArray jai = doc["ai"].to<JsonArray>();
  JsonArray jao = doc["ao"].to<JsonArray>();
  JsonArray jdi = doc["di"].to<JsonArray>();
  JsonArray jdo = doc["do"].to<JsonArray>();

  for (int i = 0; i < AI_COUNT; i++)
    jai.add(hal.ai[i]);
  for (int i = 0; i < AO_COUNT; i++)
    jao.add(hal.ao[i]);
  for (int i = 0; i < DI_COUNT; i++)
    jdi.add(hal.di[i]);
  for (int i = 0; i < DO_COUNT; i++)
    jdo.add(hal.doo[i]);

  char payload[256];
  size_t len = serializeJson(doc, payload, sizeof(payload));

  if (len == 0 || len >= sizeof(payload))
  {
    Serial.printf("[Telemetry] Payload too large or serialization failed (len=%u)\n", len);
    return;
  }

  bool sent = mqtt.publish(topicTelemetry, payload);
  if (!sent)
  {
    Serial.printf("[Telemetry] Publish FAILED (state=%d %s), payload=%s\n",
                  mqtt.state(), mqttStateStr(mqtt.state()), payload);
  }
  else
  {
    Serial.printf("[Telemetry] Sent (%u bytes): %s\n", len, payload);
  }
}

void taskTelemetry(void *pvParameters)
{
  for (;;)
  {
    if (!telemetryEnabled)
    {
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }

    if (millis() - lastTelemetryCmd < 10000)
    {
      sendTelemetry();
    }
    else
    {
      Serial.println("[Telemetry] Auto-disabled: no enable command in last 10s");
      telemetryEnabled = false;
    }

    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void initMQTT()
{
  wifiClient.setInsecure();
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(mqttCallback);
  mqtt.setBufferSize(MQTT_BUFFER_SIZE);
  mqtt.setKeepAlive(30);
  mqtt.setSocketTimeout(20);

  snprintf(topicControl, sizeof(topicControl), "device/%s/control",
           DEVICE_ID.c_str());
  snprintf(topicOta, sizeof(topicOta), "device/%s/ota", DEVICE_ID.c_str());
  snprintf(topicLogic, sizeof(topicLogic), "device/%s/logic",
           DEVICE_ID.c_str());
  snprintf(topicStatus, sizeof(topicStatus), "device/%s/status",
           DEVICE_ID.c_str());

  Serial.printf("[MQTT] Init: host=%s port=%d bufferSize=%u keepAlive=30s\n",
                MQTT_HOST, MQTT_PORT, MQTT_BUFFER_SIZE);
  Serial.printf("[MQTT] Topics: control=%s ota=%s logic=%s status=%s\n",
                topicControl, topicOta, topicLogic, topicStatus);
}