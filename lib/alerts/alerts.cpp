#include "alerts.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <app_state.h>
#include <credentials.h>
#include <hal.h>

ChannelLimit aiLimits[AI_COUNT];
ChannelLimit aoLimits[AO_COUNT];
QueueHandle_t alertQueue;

char diName[DI_COUNT][32];
bool diNotifyMobile[DI_COUNT];
bool diNotifyEmail[DI_COUNT];
bool diNotifySms[DI_COUNT];
bool diLastState[DI_COUNT];
int  diTrigger[DI_COUNT];

char doName[DO_COUNT][32];
bool doNotifyMobile[DO_COUNT];
bool doNotifyEmail[DO_COUNT];
bool doNotifySms[DO_COUNT];
bool doLastState[DO_COUNT];
int  doTrigger[DO_COUNT];

static inline void enqueueAlert(float value, const char *name) {
  if (!alertQueue)
    return;

  AlertJob job{};
  job.value = value;
  strncpy(job.limit.name, name, sizeof(job.limit.name) - 1);

  xQueueSend(alertQueue, &job, 0);
}

static bool sendAlertToBackend(float value, const ChannelLimit &c) {
  if (WiFi.status() != WL_CONNECTED)
    return false;

  HTTPClient http;
  WiFiClientSecure client;
  client.setInsecure();

  http.begin(client, BACKEND_URL "/send-notification");
  http.addHeader("Content-Type", "application/json");
  http.addHeader("x-api-key", BACKEND_TOKEN);

  JsonDocument doc;
  doc["deviceid"] = DEVICE_ID;

  char msg[51];
  snprintf(msg, sizeof(msg), "%s out of range: %.2f",
           c.name[0] ? c.name : "Channel", value);

  doc["message"] = msg;

  String payload;
  serializeJson(doc, payload);

  int code = http.POST(payload);
  http.end();

  if (code < 200 || code >= 300) {
    Serial.printf("Alert POST failed (HTTP %d)\n", code);
    return false;
  }
  return true;
}

void checkDiAlerts() {
  for (int i = 0; i < DI_COUNT; i++) {
    bool current = hal.di[i];
    bool last = diLastState[i];

    if (current != last && (int)current == diTrigger[i]) {
      if (diNotifyMobile[i] || diNotifyEmail[i] || diNotifySms[i]) {
        enqueueAlert(current ? 1.0f : 0.0f, diName[i]);
      }
    }

    diLastState[i] = current;
  }
}

void checkDoAlerts() {
  for (int i = 0; i < DO_COUNT; i++) {
    bool current = hal.doo[i];
    bool last = doLastState[i];

    if (current != last && (int)current == doTrigger[i]) {
      if (doNotifyMobile[i] || doNotifyEmail[i] || doNotifySms[i]) {
        enqueueAlert(current ? 1.0f : 0.0f, doName[i]);
      }
    }

    doLastState[i] = current;
  }
}

void checkAiLimits() {
  uint32_t now = millis();

  for (int i = 0; i < AI_COUNT; i++) {
    float v = hal.ai[i];
    ChannelLimit &c = aiLimits[i];

    bool isOut = (v < c.min || v > c.max);

    if (isOut && !c.outOfRange) {
      c.outOfRange = true;
      c.lastNotifyMs = 0;
    }

    if (!isOut && c.outOfRange) {
      c.outOfRange = false;
    }

    if (c.outOfRange && now - c.lastNotifyMs >= 300000) {
      enqueueAlert(v, c.name);
      c.lastNotifyMs = now;
    }
  }
}

void checkAoLimits() {
  uint32_t now = millis();

  for (int i = 0; i < AO_COUNT; i++) {
    float v = hal.ao[i];
    ChannelLimit &c = aoLimits[i];

    bool isOut = (v < c.min || v > c.max);

    if (isOut && !c.outOfRange) {
      c.outOfRange = true;
      c.lastNotifyMs = 0;
    }

    if (!isOut && c.outOfRange) {
      c.outOfRange = false;
    }

    if (c.outOfRange && now - c.lastNotifyMs >= 300000) {
      enqueueAlert(v, c.name);
      c.lastNotifyMs = now;
    }
  }
}

#define NVS_MAX_ALERTS 8

/**
 * Persists an AlertJob to NVS so it survives reboots and Wi-Fi outages.
 * When the queue is full the oldest entry is evicted (FIFO).
 */
static void saveAlertToNVS(const AlertJob &job) {
  Preferences prefs;
  prefs.begin("alerts", false);

  uint8_t cnt = prefs.getUChar("cnt", 0);
  if (cnt >= NVS_MAX_ALERTS) {
    // Evict oldest: shift slots 1..N-1 down to 0..N-2
    for (uint8_t i = 0; i < NVS_MAX_ALERTS - 1; i++) {
      char from[4], to[4];
      snprintf(from, sizeof(from), "a%d", i + 1);
      snprintf(to,   sizeof(to),   "a%d", i);
      prefs.putString(to, prefs.getString(from, ""));
    }
    cnt = NVS_MAX_ALERTS - 1;
  }

  char key[4];
  snprintf(key, sizeof(key), "a%d", cnt);

  char buf[80];
  snprintf(buf, sizeof(buf), "{\"v\":%.4f,\"n\":\"%s\"}",
           job.value, job.limit.name[0] ? job.limit.name : "Channel");

  prefs.putString(key, buf);
  prefs.putUChar("cnt", cnt + 1);
  prefs.end();

  Serial.printf("Alert persisted to NVS (slot %d, total %d)\n", cnt, cnt + 1);
}

/**
 * Attempts to resend all alerts stored in NVS.
 * Alerts that succeed are removed; failures are kept for the next retry.
 */
static void replayPendingAlerts() {
  if (WiFi.status() != WL_CONNECTED)
    return;

  Preferences prefs;
  prefs.begin("alerts", false);
  uint8_t cnt = prefs.getUChar("cnt", 0);

  if (cnt == 0) {
    prefs.end();
    return;
  }

  Serial.printf("Replaying %d pending alert(s) from NVS\n", cnt);

  String unsent[NVS_MAX_ALERTS];
  uint8_t remaining = 0;

  for (uint8_t i = 0; i < cnt; i++) {
    char key[4];
    snprintf(key, sizeof(key), "a%d", i);
    String json = prefs.getString(key, "");
    if (json.length() == 0) continue;

    JsonDocument doc;
    if (deserializeJson(doc, json) != DeserializationError::Ok) continue;

    AlertJob job{};
    job.value = doc["v"] | 0.0f;
    strncpy(job.limit.name, doc["n"] | "", sizeof(job.limit.name) - 1);

    if (!sendAlertToBackend(job.value, job.limit)) {
      if (remaining < NVS_MAX_ALERTS)
        unsent[remaining++] = json; // keep for next retry
    }
  }

  // Rewrite NVS with only the unsent alerts
  for (uint8_t i = 0; i < NVS_MAX_ALERTS; i++) {
    char key[4];
    snprintf(key, sizeof(key), "a%d", i);
    if (i < remaining)
      prefs.putString(key, unsent[i]);
    else
      prefs.remove(key);
  }
  prefs.putUChar("cnt", remaining);
  prefs.end();

  Serial.printf("NVS replay: %d sent, %d remaining\n", cnt - remaining, remaining);
}

void taskAlerts(void *) {
  AlertJob job;
  uint32_t lastReplayMs = 0;

  replayPendingAlerts(); // send anything that survived a reboot

  for (;;) {
    // Retry unsent alerts every 30 s
    if (millis() - lastReplayMs >= 30000) {
      lastReplayMs = millis();
      replayPendingAlerts();
    }

    if (xQueueReceive(alertQueue, &job, pdMS_TO_TICKS(1000))) {
      if (!sendAlertToBackend(job.value, job.limit)) {
        saveAlertToNVS(job); // Wi-Fi down or server error — persist for later
      }
    }
  }
}
