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
const int PIN_DO[DO_COUNT] = {23, 19, 18, 5}; // Digital outputs
const int PIN_AO[AO_COUNT] = {15, -1, -1, -1}; // Analog outputs (PWM)

// PWM settings — one LEDC channel per AO output
const int PWM_CH[AO_COUNT] = {0, 1, 2, 3};
const int PWM_FREQ = 1;
const int PWM_RES  = 8;

#endif

// ============================================================
//  ADC / signal conversion constants
// ============================================================
#define ADC_MAX 4095.0f
#define VREF 3.3f
#define SHUNT_OHMS 150.0f

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
