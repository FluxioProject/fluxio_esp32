#pragma once

// ============================================================
//  IO channel counts
// ============================================================
// Uncomment to use simulated IO instead of real hardware
// #define IO_SIMULATION

#define AI_COUNT 4
#define AO_COUNT 4
#define DI_COUNT 4
#define DO_COUNT 4

// ============================================================
//  Physical pin assignments (real IO only)
//  Use -1 for channels not wired on the target board.
// ============================================================
#ifndef IO_SIMULATION

const int PIN_AI[AI_COUNT] = {35,  34,  -1,  -1};  // Analog inputs  (0-10 V engineering scale) — ADC1 channels
const int PIN_DI[DI_COUNT] = {36, 39, 32, 33}; // Digital inputs
const int PIN_DO[DO_COUNT] = {4, 16, 17, 18}; // Digital outputs
const int PIN_AO[AO_COUNT] = {15, -1, -1, -1}; // Analog outputs (PWM)

// PWM settings — one LEDC channel per AO output
const int PWM_CH[AO_COUNT] = {0, 1, 2, 3};
const int PWM_FREQ = 5000;
const int PWM_RES  = 8;

#endif

// ============================================================
//  ADC / signal conversion constants
// ============================================================
#define ADC_MAX 4095.0f
#define VREF 3.3f
#define SHUNT_OHMS 150.0f // used only by AO (still 4-20mA output loop)

// ----- Analog Input (AI) — 0-10V voltage divider -----
// R19/R21 = series resistor (68K), R20/R22 = pulldown to GND (33K)
// Divider ratio = R_PULLDOWN / (R_SERIES + R_PULLDOWN)
#define AI_R_SERIES 68000.0f
#define AI_R_PULLDOWN 33000.0f
#define AI_DIVIDER_RATIO (AI_R_PULLDOWN / (AI_R_SERIES + AI_R_PULLDOWN))
#define AI_INPUT_MAX_VOLTS 10.0f

// Number of ADC samples averaged per reading, to filter ESP32 ADC noise
#define AI_OVERSAMPLE 16

// ============================================================
//  EEPROM layout
// ============================================================
#define EEPROM_SIZE 1024
#define SSID_ADDRESS 0
#define SSID_MAX_LENGTH 32
#define PASSWORD_ADDRESS (SSID_ADDRESS + SSID_MAX_LENGTH)
#define PASSWORD_MAX_LENGTH 32

// ============================================================
//  Misc hardware timings
// ============================================================
#define WDT_TIMER 10    // Watchdog timeout in seconds
#define TIMEOUT_WIFI 18 // Wi-Fi connect attempts before AP fallback
