#include "backend.h"
#include <credentials.h>
#include <hal.h>
#include <ota.h>
#include <mqtt.h>

SemaphoreHandle_t httpsMutex = nullptr;
WiFiClientSecure sharedHttpsClient;

void initHttpsMutex()
{
  httpsMutex = xSemaphoreCreateMutex();
}

// Timeout de espera pelo mutex — se passar disso, algo está preso e é
// melhor falhar essa chamada do que travar a task pra sempre.
static const uint32_t HTTPS_MUTEX_WAIT_MS = 20000;

bool fetchMQTTCredentials()
{
  if (WiFi.status() != WL_CONNECTED)
    return false;

  if (httpsMutex && xSemaphoreTake(httpsMutex, pdMS_TO_TICKS(HTTPS_MUTEX_WAIT_MS)) != pdTRUE)
  {
    Serial.println("[Backend] Timeout esperando mutex HTTPS (mqtt-creds)");
    return false;
  }

  delay(1000);

  const int maxAttempts = 4;
  String url = String(BACKEND_URL) + "/mqtt?deviceId=" + DEVICE_ID;
  bool result = false;

  for (int attempt = 1; attempt <= maxAttempts; attempt++)
  {
    Serial.printf("[Backend] Fetch attempt %d/%d\n", attempt, maxAttempts);

    sharedHttpsClient.stop();
    sharedHttpsClient.setCACert(GCS_ROOT_CA); // reconfigura sempre — não assume estado anterior
    sharedHttpsClient.setTimeout(8000);

    HTTPClient http;
    http.setTimeout(8000);
    http.begin(sharedHttpsClient, url);
    http.addHeader("x-api-key", BACKEND_TOKEN);
    int httpCode = http.GET();

    if (httpCode != 200)
    {
      Serial.printf("[Backend] HTTP error: %d\n", httpCode);
      if (httpCode > 0)
        Serial.println(http.getString());
      http.end();

      if (attempt < maxAttempts)
      {
        uint32_t backoffMs = attempt * 1000;
        Serial.printf("[Backend] Retrying in %lu ms\n", backoffMs);
        delay(backoffMs);
        continue;
      }
      break;
    }

    String payload = http.getString();
    http.end();

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);

    if (err)
    {
      Serial.printf("[Backend] JSON parse error: %s\n", err.c_str());
      if (attempt < maxAttempts)
      {
        uint32_t backoffMs = attempt * 1000;
        Serial.printf("[Backend] Retrying in %lu ms\n", backoffMs);
        delay(backoffMs);
        continue;
      }
      break;
    }

    strncpy(MQTT_HOST, doc["mqtt"]["host"] | "", sizeof(MQTT_HOST) - 1);
    MQTT_PORT = doc["mqtt"]["port"] | 0;
    strncpy(MQTT_USER, doc["mqtt"]["user"] | "", sizeof(MQTT_USER) - 1);
    strncpy(MQTT_PASS, doc["mqtt"]["pass"] | "", sizeof(MQTT_PASS) - 1);

    Serial.println("[Backend] MQTT credentials OK");
    result = true;
    break;
  }

  if (httpsMutex)
    xSemaphoreGive(httpsMutex);
  return result;
}

bool fetchAllChannels()
{
  if (httpsMutex && xSemaphoreTake(httpsMutex, pdMS_TO_TICKS(HTTPS_MUTEX_WAIT_MS)) != pdTRUE)
  {
    Serial.println("[Backend] Timeout esperando mutex HTTPS (channels)");
    return false;
  }

  bool result = false;
  {
    sharedHttpsClient.stop();
    sharedHttpsClient.setCACert(GCS_ROOT_CA);
    sharedHttpsClient.setTimeout(8000);

    HTTPClient http;
    String url = BACKEND_URL "/get-all-channels?deviceId=" + DEVICE_ID;

    http.begin(sharedHttpsClient, url);
    http.addHeader("x-api-key", BACKEND_TOKEN);

    int code = http.GET();
    if (code != 200)
    {
      Serial.printf("Error fetching all channels HTTP %d\n", code);
      Serial.println(http.getString());
      http.end();
    }
    else
    {
      String body = http.getString();
      http.end();

      JsonDocument doc;
      DeserializationError err = deserializeJson(doc, body);

      if (err)
      {
        Serial.println("JSON parse error (get-all-channels)");
      }
      else
      {
        // ================= AI =================
        JsonObject aiObj = doc["ai"];
        for (JsonPair kv : aiObj)
        {
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

          Serial.printf("AI[%d] map(%.2f..%.2f) lim(%.2f..%.2f) mob=%d email=%d sms=%d\n",
                        index, hal.aiMapMin[index], hal.aiMapMax[index], aiLimits[index].min,
                        aiLimits[index].max, aiLimits[index].notifyMobile,
                        aiLimits[index].notifyEmail, aiLimits[index].notifySms);
        }

        // ================= AO =================
        JsonObject aoObj = doc["ao"];
        for (JsonPair kv : aoObj)
        {
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

          Serial.printf("AO[%d] map(%.2f..%.2f) lim(%.2f..%.2f) mob=%d email=%d sms=%d\n",
                        index, hal.aoMapMin[index], hal.aoMapMax[index], aoLimits[index].min,
                        aoLimits[index].max, aoLimits[index].notifyMobile,
                        aoLimits[index].notifyEmail, aoLimits[index].notifySms);
        }

        // ================= DI =================
        JsonObject diObj = doc["di"];
        for (JsonPair kv : diObj)
        {
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
        for (JsonPair kv : doObj)
        {
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

        result = true;
        channelsSynced = true;
      }
    }
  }

  if (httpsMutex)
    xSemaphoreGive(httpsMutex);
  return result;
}

void setInitChannels()
{
  for (int i = 0; i < AI_COUNT; i++)
  {
    hal.aiMapMin[i] = 0.0f;
    hal.aiMapMax[i] = 10.0f;
    aiLimits[i].min = -999999;
    aiLimits[i].max = 999999;
    aiLimits[i].notifyMobile = false;
    aiLimits[i].notifyEmail = false;
    aiLimits[i].notifySms = false;
    aiLimits[i].outOfRange = false;
    aiLimits[i].lastNotifyMs = 0;
  }
  for (int i = 0; i < AO_COUNT; i++)
  {
    hal.aoMapMin[i] = 0.0f;
    hal.aoMapMax[i] = 10.0f;
    aoLimits[i].min = -999999;
    aoLimits[i].max = 999999;
    aoLimits[i].notifyMobile = false;
    aoLimits[i].notifyEmail = false;
    aoLimits[i].notifySms = false;
    aoLimits[i].outOfRange = false;
    aoLimits[i].lastNotifyMs = 0;
  }
  for (int i = 0; i < DI_COUNT; i++)
  {
    diLastState[i] = hal.di[i];
    diTrigger[i] = 1;
  }
  for (int i = 0; i < DO_COUNT; i++)
    doLastState[i] = hal.doo[i];

  const uint32_t timeoutMs = 25000;
  uint32_t start = millis();
  while (!mqtt.connected() && (millis() - start) < timeoutMs)
    vTaskDelay(pdMS_TO_TICKS(200));

  if (!fetchAllChannels())
    Serial.println("Failed to fetch channels, using defaults");
}

bool checkForFirmwareUpdate()
{
  if (WiFi.status() != WL_CONNECTED)
    return false;

  if (httpsMutex && xSemaphoreTake(httpsMutex, pdMS_TO_TICKS(HTTPS_MUTEX_WAIT_MS)) != pdTRUE)
  {
    Serial.println("[FW] Timeout esperando mutex HTTPS");
    return false;
  }

  bool result = false;
  {
    String baseUrl = String(BACKEND_URL);
    if (baseUrl.endsWith("/devices"))
      baseUrl.remove(baseUrl.length() - 8);

    String url = baseUrl + "/devices/" + DEVICE_ID + "/firmware/latest";

    sharedHttpsClient.stop();
    sharedHttpsClient.setCACert(GCS_ROOT_CA);
    sharedHttpsClient.setTimeout(8000);

    HTTPClient http;
    http.setTimeout(5000);

    if (!http.begin(sharedHttpsClient, url))
    {
      Serial.println("[FW] http.begin failed");
    }
    else
    {
      http.addHeader("x-api-key", BACKEND_TOKEN);
      Serial.printf("[FW] URL: %s\n", url.c_str());

      int code = http.GET();
      Serial.printf("[FW] HTTP=%d (%s)\n", code, HTTPClient::errorToString(code).c_str());

      String payload = http.getString();
      if (payload.length())
        Serial.println(payload);

      if (code == 404)
      {
        Serial.println("[FW] No firmware committed for this device yet");
        http.end();
      }
      else if (code != HTTP_CODE_OK)
      {
        http.end();
      }
      else
      {
        http.end();

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, payload);

        if (err)
        {
          Serial.printf("[FW] JSON parse error: %s\n", err.c_str());
        }
        else
        {
          String latestVersion = doc["version"] | "";
          const char *readUrl = doc["readUrl"] | "";
          const char *sha = doc["sha256"] | "";
          uint32_t size = doc["size"] | 0;

          if (latestVersion.isEmpty() || latestVersion == currentFwVersion)
          {
            Serial.println("[FW] Already up to date");
            fwChecked = true;
          }
          else if (!readUrl[0] || strlen(sha) != 64 || size == 0)
          {
            Serial.println("[FW] Invalid firmware/latest payload");
            fwChecked = true;
          }
          else
          {
            Serial.printf("[FW] Update available: %s -> %s\n",
                          currentFwVersion.c_str(), latestVersion.c_str());

            OtaJob job{};
            strncpy(job.url, readUrl, sizeof(job.url) - 1);
            strncpy(job.sha256, sha, sizeof(job.sha256) - 1);
            strncpy(job.version, latestVersion.c_str(), sizeof(job.version) - 1);
            job.size = size;

            if (!otaQueue)
            {
              Serial.println("[FW] otaQueue not initialized");
            }
            else if (xQueueSend(otaQueue, &job, 0) != pdTRUE)
            {
              Serial.println("[FW] OTA queue full");
            }
            else
            {
              Serial.printf("[FW] OTA queued (%s)\n", job.version);
              result = true;
            }
          }
        }
      }
    }
  }

  if (httpsMutex)
    xSemaphoreGive(httpsMutex);
  return result;
}

bool fetchLogicFromBackend(String &outJson, String &outUpdatedAt)
{
  if (httpsMutex && xSemaphoreTake(httpsMutex, pdMS_TO_TICKS(HTTPS_MUTEX_WAIT_MS)) != pdTRUE)
  {
    Serial.println("[Backend] Timeout esperando mutex HTTPS (logic)");
    return false;
  }

  bool ok = false;
  {
    HTTPClient http;
    sharedHttpsClient.stop();
    sharedHttpsClient.setCACert(GCS_ROOT_CA);
    sharedHttpsClient.setTimeout(8000);

    http.addHeader("x-api-key", BACKEND_TOKEN);
    String url = String(BACKEND_URL) + "/" + DEVICE_ID + "/logic-for-device";

    if (!http.begin(sharedHttpsClient, url))
    {
      Serial.println("[Backend] Falha ao iniciar conexão HTTP (logic)");
    }
    else
    {
      int code = http.GET();

      if (code != 200)
      {
        Serial.printf("[Backend] logic-for-device retornou HTTP %d\n", code);
        http.end();
      }
      else
      {
        String body = http.getString();
        http.end();

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, body);
        if (err)
        {
          Serial.printf("[Backend] JSON inválido em logic-for-device: %s\n", err.c_str());
        }
        else
        {
          outUpdatedAt = doc["updatedAt"] | "";

          JsonDocument logicDoc;
          logicDoc["v"] = doc["v"];
          logicDoc["blocks"] = doc["blocks"];

          String out;
          serializeJson(logicDoc, out);
          outJson = out;
          ok = true;
        }
      }
    }
  }

  if (httpsMutex)
    xSemaphoreGive(httpsMutex);
  return ok;
}