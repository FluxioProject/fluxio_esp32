#include "alerts.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <app_state.h>
#include <credentials.h>
#include <hal.h>

// ----- Tuning constants -----
// Minimum time a value must stay out of range before an alert is considered
// (filters transient spikes / ADC noise near the threshold).
static const uint32_t AI_DEBOUNCE_MS = 3000;

// Minimum time between two alerts for the SAME channel, even if it keeps
// flickering in and out of range repeatedly.
static const uint32_t AI_COOLDOWN_MS = 300000; // 5 minutes

// Minimum time a digital input/output must hold a candidate state before
// it's treated as a real edge (filters mechanical bounce / electrical noise).
static const uint32_t DIO_DEBOUNCE_MS = 50;

// Minimum time between two alerts for the same DI/DO channel.
static const uint32_t DIO_COOLDOWN_MS = 60000; // 1 minute

ChannelLimit aiLimits[AI_COUNT];
ChannelLimit aoLimits[AO_COUNT];
QueueHandle_t alertQueue;

char diName[DI_COUNT][32];
bool diNotifyMobile[DI_COUNT];
bool diNotifyEmail[DI_COUNT];
bool diNotifySms[DI_COUNT];
bool diLastState[DI_COUNT];
int  diTrigger[DI_COUNT];
uint32_t diLastNotifyMs[DI_COUNT];
uint32_t diPendingSinceMs[DI_COUNT];

char doName[DO_COUNT][32];
bool doNotifyMobile[DO_COUNT];
bool doNotifyEmail[DO_COUNT];
bool doNotifySms[DO_COUNT];
bool doLastState[DO_COUNT];
int  doTrigger[DO_COUNT];
uint32_t doLastNotifyMs[DO_COUNT];
uint32_t doPendingSinceMs[DO_COUNT];

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

// ----- Digital edge alerts (DI / DO) with debounce + cooldown -----

static void checkDigitalAlerts(int count, const bool *current, bool *lastState,
                               const int *trigger, bool *notifyMobile,
                               bool *notifyEmail, bool *notifySms,
                               char (*name)[32], uint32_t *lastNotifyMs,
                               uint32_t *pendingSinceMs) {
  uint32_t now = millis();

  for (int i = 0; i < count; i++) {
    bool value = current[i];

    if (value != lastState[i]) {
      // Candidate state changed — start (or restart) the debounce timer.
      pendingSinceMs[i] = now;
      lastState[i] = value;
      continue;
    }

    // State has been stable since pendingSinceMs. Only treat it as a
    // confirmed edge once it has held for at least DIO_DEBOUNCE_MS.
    if (pendingSinceMs[i] == 0 || now - pendingSinceMs[i] < DIO_DEBOUNCE_MS)
      continue;

    if ((int)value != trigger[i])
      continue;

    // Confirmed edge matching the configured trigger — but still respect
    // a per-channel cooldown so a channel that keeps toggling doesn't spam.
    if (now - lastNotifyMs[i] < DIO_COOLDOWN_MS)
      continue;

    if (notifyMobile[i] || notifyEmail[i] || notifySms[i]) {
      enqueueAlert(value ? 1.0f : 0.0f, name[i]);
      lastNotifyMs[i] = now;
    }

    // Prevent re-triggering again until the state changes once more.
    pendingSinceMs[i] = 0;
  }
}

void checkDiAlerts() {
  bool current[DI_COUNT];
  for (int i = 0; i < DI_COUNT; i++) current[i] = hal.di[i];

  checkDigitalAlerts(DI_COUNT, current, diLastState, diTrigger, diNotifyMobile,
                     diNotifyEmail, diNotifySms, diName, diLastNotifyMs,
                     diPendingSinceMs);
}

void checkDoAlerts() {
  bool current[DO_COUNT];
  for (int i = 0; i < DO_COUNT; i++) current[i] = hal.doo[i];

  checkDigitalAlerts(DO_COUNT, current, doLastState, doTrigger, doNotifyMobile,
                     doNotifyEmail, doNotifySms, doName, doLastNotifyMs,
                     doPendingSinceMs);
}

// ----- Analog limit alerts (AI / AO) with hysteresis, debounce + cooldown -----

static void checkAnalogLimits(int count, const float *values, ChannelLimit *limits) {
  uint32_t now = millis();

  for (int i = 0; i < count; i++) {
    float v = values[i];
    ChannelLimit &c = limits[i];

    bool isOut = (v < c.min || v > c.max);

    if (isOut) {
      if (!c.outOfRange) {
        // Just left the valid range — start debounce, do not alert yet.
        c.outOfRange = true;
        c.firstOutOfRangeMs = now;
      }

      bool debounced = (now - c.firstOutOfRangeMs) >= AI_DEBOUNCE_MS;
      bool cooldownElapsed = (now - c.lastNotifyMs) >= AI_COOLDOWN_MS;

      // Only alert once the condition has held for the debounce window AND
      // the cooldown since the last alert for this channel has elapsed.
      // lastNotifyMs starts at 0, so the very first alert fires as soon as
      // the debounce window passes.
      if (debounced && cooldownElapsed) {
        enqueueAlert(v, c.name);
        c.lastNotifyMs = now;
      }
    } else {
      // Back inside range — reset debounce state, but keep lastNotifyMs so
      // the cooldown still applies if it flickers back out again shortly.
      c.outOfRange = false;
      c.firstOutOfRangeMs = 0;
    }
  }
}

void checkAiLimits() {
  checkAnalogLimits(AI_COUNT, hal.ai, aiLimits);
}

void checkAoLimits() {
  checkAnalogLimits(AO_COUNT, hal.ao, aoLimits);
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
        unsent[remaining++] = json;
    }
  }

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

  replayPendingAlerts();

  for (;;) {
    if (millis() - lastReplayMs >= 30000) {
      lastReplayMs = millis();
      replayPendingAlerts();
    }

    if (xQueueReceive(alertQueue, &job, pdMS_TO_TICKS(1000))) {
      if (!sendAlertToBackend(job.value, job.limit)) {
        saveAlertToNVS(job);
      }
    }
  }
}