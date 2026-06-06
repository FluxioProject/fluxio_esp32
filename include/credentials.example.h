#pragma once

// Copy this file to credentials.h and fill in your values.
// credentials.h is gitignored and should never be committed.

// Wi-Fi fallback AP credentials (used when station connection fails)
#define SSID_ESP "ESP32"
#define PASSWORD_ESP "change_me"

// Default Wi-Fi station credentials (overridden by EEPROM after first save)
#define DEFAULT_WIFI_SSID "your_wifi_ssid"
#define DEFAULT_WIFI_PASSWORD "your_wifi_password"

// Backend API
#define BACKEND_URL "https://your-backend-url/api/devices"
#define BACKEND_TOKEN "your_api_token_here"

// Root CA certificate for HiveMQ Cloud broker (Let's Encrypt / ISRG Root X1)
// Used by the MQTT task to verify the TLS connection to the broker.
// Retrieve with: openssl s_client -connect <your-broker>:8883 -showcerts
// then copy the last certificate block (root CA) below.
static const char HIVEMQ_ROOT_CA[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
<paste broker root CA cert here>
-----END CERTIFICATE-----
)EOF";

// Root CA certificate for storage.googleapis.com (Google Trust Services WR2)
// Used by the OTA task to verify the TLS connection when downloading firmware.
// Retrieve with: openssl s_client -connect storage.googleapis.com:443 -showcerts
// then copy the last certificate block (root CA) below.
// Check expiry with: openssl x509 -noout -dates -in <cert.pem>
static const char GCS_ROOT_CA[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
<paste root CA cert here>
-----END CERTIFICATE-----
)EOF";
