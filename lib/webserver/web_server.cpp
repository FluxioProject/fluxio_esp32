#include "web_server.h"
#include "esp_task_wdt.h"
#include "pages.h"
#include <eeprom_manager.h>

MyWebServer webServer;

void MyWebServer::myDelayMillis(uint16_t delay)
{
  vTaskDelay(pdMS_TO_TICKS(delay));
}

void MyWebServer::createAP()
{
  WiFi.softAP(SSID_ESP, PASSWORD_ESP);
  IPAddress IP = WiFi.softAPIP();

  apActive = true;
  apStartTime = millis();

  Serial.println("AP created! " + IP.toString());
}

void MyWebServer::connectWiFi()
{
  Serial.println("SSID: " + ssid + " Password: " + password);

  WiFi.begin(ssid.c_str(), password.c_str());

  uint8_t wifiRetries = 0;
  while (WiFi.status() != WL_CONNECTED)
  {
    esp_task_wdt_reset();
    wifiRetries++;
    if (wifiRetries >= TIMEOUT_WIFI)
    {
      createAP();
      myDelayMillis(50);
      wifiRetries = 0;
      break;
    }
    myDelayMillis(500);
  }

  if (WiFi.isConnected())
  {
    // se o AP tinha sido ligado antes, desliga agora que temos internet
    if (apActive)
    {
      WiFi.softAPdisconnect(true);
      apActive = false;
      apStartTime = 0;
      Serial.println("AP turn off — STA.");
    }

    String ip = WiFi.localIP().toString();
    Serial.println("Connected - IP: " + ip);
  }
  else
  {
    Serial.println("Failed to connect to Wi-Fi. AP mode activated.");
  }

  myDelayMillis(500);
  setupRoutes();
}

void MyWebServer::setupRoutes()
{
  server.on("/", HTTP_GET, std::bind(&MyWebServer::handleRoot, this));
  server.on("/wifi", HTTP_GET, std::bind(&MyWebServer::handleWiFiForm, this));
  server.on("/wifi", HTTP_POST, std::bind(&MyWebServer::handleWiFiForm, this));
  server.on("/firmware", HTTP_GET,
            std::bind(&MyWebServer::handleFirmwareUpload, this));
  server.on(
      "/upload", HTTP_POST, []() {},
      std::bind(&MyWebServer::handleUploadFirmware, this));

  server.begin();
  serverStartTime = millis();
}

void MyWebServer::handleClient() { server.handleClient(); }

void MyWebServer::close() { server.close(); }

void MyWebServer::handleRoot()
{
  String page = principal;
  page.replace("{{DEVICE_ID}}", DEVICE_ID);
  server.send(200, "text/html", page);
}

void MyWebServer::handleWiFiForm()
{
  if (server.method() == HTTP_POST)
  {
    String newSSID = server.arg("ssid");
    String newPassword = server.arg("password");

    eepromManager.saveStringEEPROM(SSID_ADDRESS, newSSID, SSID_MAX_LENGTH);
    eepromManager.saveStringEEPROM(PASSWORD_ADDRESS, newPassword,
                                   PASSWORD_MAX_LENGTH);

    server.send(
        200, "text/html",
        "<html><body><h1>Wi-Fi updated. Rebooting...</h1></body></html>");

    delay(1000);
    ESP.restart();
    return;
  }
  server.send(200, "text/html", wifi);
}

void MyWebServer::handleUploadFirmware()
{
  HTTPUpload &upload = server.upload();

  if (upload.status == UPLOAD_FILE_START)
  {
    String filename = upload.filename;
    if (!filename.endsWith(".bin"))
    {
      server.send(400, "text/html",
                  "<h1>Invalid file type. Only .bin files are allowed.</h1>");
      return;
    }

    if (!Update.begin(UPDATE_SIZE_UNKNOWN))
    {
      server.send(500, "text/html", "<h1>Failed to begin update!</h1>");
      return;
    }
  }
  else if (upload.status == UPLOAD_FILE_WRITE)
  {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize)
    {
      server.send(500, "text/html", "<h1>Failed to write update!</h1>");
      return;
    }
  }
  else if (upload.status == UPLOAD_FILE_END)
  {
    if (Update.end(true))
    {
      server.send(200, "text/html",
                  "<h1>Firmware uploaded and updated successfully!</h1>");
      ESP.restart();
    }
    else
    {
      server.send(500, "text/html", "<h1>Failed to finalize update!</h1>");
    }
  }
}

void MyWebServer::handleFirmwareUpload()
{
  server.send(200, "text/html", uploadFW);
}

void wifiandwdtTask(void *pvParameters)
{
  esp_task_wdt_add(NULL);

  while (true)
  {
    esp_task_wdt_reset();

    reconnectWiFi++;
    if (reconnectWiFi >= 15)
    {
      if (WiFi.status() != WL_CONNECTED)
      {
        Serial.println("[WiFi] Disconnected, reconnecting...");
        webServer.connectWiFi();
      }
      else
      {
        Serial.printf("[WiFi] OK, RSSI=%d dBm\n", WiFi.RSSI());
      }

      reconnectWiFi = 0;
    }

    if (apActive &&
        (millis() - apStartTime >= 120000))
    {
      Serial.println("[WiFi] AP active for 2 minutes. Restarting...");
      ESP.restart();
    }

    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}