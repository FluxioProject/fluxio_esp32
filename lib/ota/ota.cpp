#include "ota.h"
#include <HTTPClient.h>
#include <Update.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <credentials.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <mbedtls/sha256.h>
#include <app_state.h>

QueueHandle_t otaQueue = nullptr;

static void bytesToHex(const uint8_t *in, size_t len, char *outHex65)
{
  static const char *hex = "0123456789abcdef";
  for (size_t i = 0; i < len; i++)
  {
    outHex65[i * 2] = hex[(in[i] >> 4) & 0xF];
    outHex65[i * 2 + 1] = hex[in[i] & 0xF];
  }
  outHex65[len * 2] = '\0';
}

bool doOtaHttps(const OtaJob &job)
{
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("OTA: no WiFi");
    return false;
  }

  WiFiClientSecure client;
  client.setCACert(GCS_ROOT_CA);
  // client.setInsecure();

  HTTPClient https;
  https.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  https.setTimeout(5000);

  Serial.println("OTA: starting GET...");
  if (!https.begin(client, job.url))
  {
    Serial.println("OTA: https.begin failed");
    return false;
  }

  int code = https.GET();
  if (code != HTTP_CODE_OK)
  {
    Serial.printf("OTA: HTTP %d\n", code);
    https.end();
    return false;
  }

  int contentLen = https.getSize();
  Serial.printf("OTA: contentLen=%d expected=%u\n", contentLen, job.size);

  if (contentLen > 0 && (uint32_t)contentLen != job.size)
  {
    Serial.println("OTA: size differs from expected");
    https.end();
    return false;
  }

  if (!Update.begin(job.size))
  {
    Serial.printf("OTA: Update.begin failed, err=%d\n", Update.getError());
    https.end();
    return false;
  }

  mbedtls_sha256_context shaCtx;
  mbedtls_sha256_init(&shaCtx);
  mbedtls_sha256_starts_ret(&shaCtx, 0);

  WiFiClient *stream = https.getStreamPtr();
  uint8_t buf[2048];
  uint32_t writtenTotal = 0;

  while (https.connected() &&
         (contentLen > 0 ? writtenTotal < (uint32_t)contentLen : true))
  {
    size_t avail = stream->available();
    // Serial.printf("avail=%u written=%u\n", avail, writtenTotal);

    if (!avail)
    {
      vTaskDelay(pdMS_TO_TICKS(10));
      if (contentLen < 0 && !https.connected())
        break;
      continue;
    }

    int toRead = (avail > sizeof(buf)) ? sizeof(buf) : (int)avail;
    int r = stream->readBytes(buf, toRead);
    // Serial.printf("read=%d\n", r);

    if (r <= 0)
      break;

    mbedtls_sha256_update_ret(&shaCtx, buf, r);

    size_t w = Update.write(buf, r);
    // Serial.printf("write=%u\n", w);
    if (w != (size_t)r)
    {
      Serial.printf("OTA: Update.write failed, err=%d\n", Update.getError());
      Update.abort();
      https.end();
      mbedtls_sha256_free(&shaCtx);
      return false;
    }

    writtenTotal += r;
    // Serial.printf("written=%u/%u\n", writtenTotal, job.size);

    if (writtenTotal > job.size)
    {
      Serial.println("OTA: size exceeds expected (abort)");
      Update.abort();
      https.end();
      mbedtls_sha256_free(&shaCtx);
      return false;
    }
  }

  https.end();

  if (writtenTotal != job.size)
  {
    Serial.printf("OTA: final size invalid: %u != %u\n", writtenTotal,
                  job.size);
    Update.abort();
    mbedtls_sha256_free(&shaCtx);
    return false;
  }

  uint8_t hashOut[32];
  mbedtls_sha256_finish_ret(&shaCtx, hashOut);
  mbedtls_sha256_free(&shaCtx);

  char hashHex[65];
  bytesToHex(hashOut, 32, hashHex);

  Serial.printf("OTA: expected sha256=%s\n", job.sha256);
  Serial.printf("OTA: computed sha256=%s\n", hashHex);

  if (strcasecmp(hashHex, job.sha256) != 0)
  {
    Serial.println("OTA: SHA256 mismatch, aborting");
    Update.abort();
    return false;
  }

  if (!Update.end(true))
  {
    Serial.printf("OTA: Update.end failed, err=%d\n", Update.getError());
    return false;
  }

  saveFirmwareVersion(String(job.version));

  Serial.println("OTA: SUCCESS! Restarting...");
  delay(500);
  ESP.restart();
  return true;
}

void taskOTA(void *pv)
{
  OtaJob job{};
  for (;;)
  {
    if (!otaQueue)
    {
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }

    if (xQueueReceive(otaQueue, &job, portMAX_DELAY) == pdTRUE)
    {
      Serial.printf("OTA: job received version=%s\n", job.version);
      bool ok = doOtaHttps(job);
      Serial.printf("OTA: finished ok=%d\n", ok ? 1 : 0);
    }
  }
}
