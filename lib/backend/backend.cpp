#include "backend.h"
#include <WiFiClientSecure.h>
#include <credentials.h>
#include <hal.h>


bool fetchMQTTCredentials() {
  if (WiFi.status() != WL_CONNECTED)
    return false;

  HTTPClient http;

  String url = String(BACKEND_URL) + "/mqtt?deviceId=" + DEVICE_ID;

  http.begin(url);
  http.addHeader("x-api-key", BACKEND_TOKEN);
  int httpCode = http.GET();

  if (httpCode != 200) {
    Serial.println("HTTP error: " + String(httpCode));
    Serial.println(http.getString());
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload);

  if (err) {
    Serial.println("JSON parse error");
    return false;
  }

  strncpy(MQTT_HOST, doc["mqtt"]["host"] | "", sizeof(MQTT_HOST) - 1);
  MQTT_PORT = doc["mqtt"]["port"] | 0;
  strncpy(MQTT_USER, doc["mqtt"]["user"] | "", sizeof(MQTT_USER) - 1);
  strncpy(MQTT_PASS, doc["mqtt"]["pass"] | "", sizeof(MQTT_PASS) - 1);

  Serial.println("MQTT credentials OK");
  return true;
}

bool fetchAllChannels() {
  HTTPClient http;
  WiFiClientSecure client;
  client.setInsecure();

  String url = BACKEND_URL "/get-all-channels?deviceId=" + DEVICE_ID;

  http.begin(client, url);
  http.addHeader("x-api-key", BACKEND_TOKEN);

  int code = http.GET();
  if (code != 200) {
    Serial.printf("Error fetching all channels HTTP %d\n", code);
    Serial.println(http.getString());
    http.end();
    return false;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getString());
  http.end();

  if (err) {
    Serial.println("JSON parse error (get-all-channels)");
    return false;
  }

  // ================= AI =================
  JsonObject aiObj = doc["ai"];
  for (JsonPair kv : aiObj) {
    int index = atoi(kv.key().c_str());
    if (index < 0 || index >= AI_COUNT)
      continue;

    JsonObject c = kv.value();

    const char *name = c["channelName"] | "";
    strncpy(aiLimits[index].name, name, sizeof(aiLimits[index].name) - 1);
    aiLimits[index].name[sizeof(aiLimits[index].name) - 1] = '\0';

    hal.aiMapMin[index] = c["mapMin"] | hal.aiMapMin[index];
    hal.aiMapMax[index] = c["mapMax"] | hal.aiMapMax[index];

    aiLimits[index].min = c["min"] | -999999;
    aiLimits[index].max = c["max"] | 999999;

    aiLimits[index].notifyMobile = c["notifyMobile"] | false;
    aiLimits[index].notifyEmail = c["notifyEmail"] | false;
    aiLimits[index].notifySms = c["notifySms"] | false;

    aiLimits[index].outOfRange = false;
    aiLimits[index].lastNotifyMs = 0;

    Serial.printf(
        "AI[%d] map(%.2f..%.2f) lim(%.2f..%.2f) mob=%d email=%d sms=%d\n",
        index, hal.aiMapMin[index], hal.aiMapMax[index], aiLimits[index].min,
        aiLimits[index].max, aiLimits[index].notifyMobile,
        aiLimits[index].notifyEmail, aiLimits[index].notifySms);
  }

  // ================= AO =================
  JsonObject aoObj = doc["ao"];
  for (JsonPair kv : aoObj) {
    int index = atoi(kv.key().c_str());
    if (index < 0 || index >= AO_COUNT)
      continue;

    JsonObject c = kv.value();

    const char *name = c["channelName"] | "";
    strncpy(aoLimits[index].name, name, sizeof(aoLimits[index].name) - 1);
    aoLimits[index].name[sizeof(aoLimits[index].name) - 1] = '\0';

    hal.aoMapMin[index] = c["mapMin"] | hal.aoMapMin[index];
    hal.aoMapMax[index] = c["mapMax"] | hal.aoMapMax[index];

    aoLimits[index].min = c["min"] | -999999;
    aoLimits[index].max = c["max"] | 999999;

    aoLimits[index].notifyMobile = c["notifyMobile"] | false;
    aoLimits[index].notifyEmail = c["notifyEmail"] | false;
    aoLimits[index].notifySms = c["notifySms"] | false;

    aoLimits[index].outOfRange = false;
    aoLimits[index].lastNotifyMs = 0;

    Serial.printf(
        "AO[%d] map(%.2f..%.2f) lim(%.2f..%.2f) mob=%d email=%d sms=%d\n",
        index, hal.aoMapMin[index], hal.aoMapMax[index], aoLimits[index].min,
        aoLimits[index].max, aoLimits[index].notifyMobile,
        aoLimits[index].notifyEmail, aoLimits[index].notifySms);
  }

  // ================= DI =================
  JsonObject diObj = doc["di"];
  for (JsonPair kv : diObj) {
    int index = atoi(kv.key().c_str());
    if (index < 0 || index >= DI_COUNT)
      continue;

    JsonObject c = kv.value();

    const char *name = c["channelName"] | "";
    strncpy(diName[index], name, sizeof(diName[index]) - 1);
    diName[index][sizeof(diName[index]) - 1] = '\0';

    diNotifyMobile[index] = c["notifyMobile"] | false;
    diNotifyEmail[index] = c["notifyEmail"] | false;
    diNotifySms[index] = c["notifySms"] | false;

    Serial.printf("DI[%d] name=%s mob=%d email=%d sms=%d\n", index,
                  diName[index], diNotifyMobile[index], diNotifyEmail[index],
                  diNotifySms[index]);
  }

  // ================= DO =================
  JsonObject doObj = doc["do"];
  for (JsonPair kv : doObj) {
    int index = atoi(kv.key().c_str());
    if (index < 0 || index >= DO_COUNT)
      continue;

    JsonObject c = kv.value();

    const char *name = c["channelName"] | "";
    strncpy(doName[index], name, sizeof(doName[index]) - 1);
    doName[index][sizeof(doName[index]) - 1] = '\0';

    doNotifyMobile[index] = c["notifyMobile"] | false;
    doNotifyEmail[index] = c["notifyEmail"] | false;
    doNotifySms[index] = c["notifySms"] | false;

    Serial.printf("DO[%d] name=%s mob=%d email=%d sms=%d\n", index,
                  doName[index], doNotifyMobile[index], doNotifyEmail[index],
                  doNotifySms[index]);
  }

  return true;
}

void setInitChannels() {
  for (int i = 0; i < AI_COUNT; i++) {
    hal.aiMapMin[i] = 0.0f;
    hal.aiMapMax[i] = 100.0f;
    aiLimits[i].min = -999999;
    aiLimits[i].max = 999999;
    aiLimits[i].notifyMobile = false;
    aiLimits[i].notifyEmail = false;
    aiLimits[i].notifySms = false;
    aiLimits[i].outOfRange = false;
    aiLimits[i].lastNotifyMs = 0;
  }
  for (int i = 0; i < AO_COUNT; i++) {
    hal.aoMapMin[i] = 0.0f;
    hal.aoMapMax[i] = 100.0f;
    aoLimits[i].min = -999999;
    aoLimits[i].max = 999999;
    aoLimits[i].notifyMobile = false;
    aoLimits[i].notifyEmail = false;
    aoLimits[i].notifySms = false;
    aoLimits[i].outOfRange = false;
    aoLimits[i].lastNotifyMs = 0;
  }
  for (int i = 0; i < DI_COUNT; i++) {
    diLastState[i] = hal.di[i];
    diTrigger[i] = 1;
  }
  for (int i = 0; i < DO_COUNT; i++)
    doLastState[i] = hal.doo[i];

  if (!fetchAllChannels())
    Serial.println("Failed to fetch channels, using defaults");
}
