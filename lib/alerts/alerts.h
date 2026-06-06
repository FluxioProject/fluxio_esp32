#pragma once

#include <app_state.h>
#include <hw_config.h>

/**
 * @brief Configuration and runtime state for a single analog channel alert.
 *
 * Populated by fetchAllChannels() and evaluated on every logic task cycle.
 */
typedef struct {
  float min;             ///< Lower alert threshold (engineering units).
  float max;             ///< Upper alert threshold (engineering units).
  bool notifyMobile;     ///< Send push notification when limit is breached.
  bool notifyEmail;      ///< Send e-mail notification when limit is breached.
  bool notifySms;        ///< Send SMS notification when limit is breached.
  char name[32];         ///< Human-readable channel name.
  bool outOfRange;       ///< True while the channel value is outside [min, max].
  uint32_t lastNotifyMs; ///< millis() timestamp of the last alert sent (rate-limits to 5 min).
} ChannelLimit;

/**
 * @brief Payload queued to taskAlerts when an alert condition is detected.
 *
 * Passed from the logic task to the alert task via alertQueue to avoid
 * blocking HTTP calls inside the time-critical logic loop.
 */
typedef struct {
  float value;       ///< IO value that triggered the alert.
  ChannelLimit limit; ///< Copy of the channel's ChannelLimit at the time of the alert.
} AlertJob;

/** FreeRTOS queue used to hand off AlertJob items from the logic task to taskAlerts. */
extern QueueHandle_t alertQueue;

// ----- Analog channel limits -----
extern ChannelLimit aiLimits[AI_COUNT]; ///< Alert limits for each analog input channel.
extern ChannelLimit aoLimits[AO_COUNT]; ///< Alert limits for each analog output channel.

// ----- DI channel metadata -----
extern char diName[DI_COUNT][32];      ///< Human-readable name for each digital input.
extern bool diNotifyMobile[DI_COUNT];  ///< Enable push notification for DI edge events.
extern bool diNotifyEmail[DI_COUNT];   ///< Enable e-mail notification for DI edge events.
extern bool diNotifySms[DI_COUNT];     ///< Enable SMS notification for DI edge events.
extern bool diLastState[DI_COUNT];     ///< Last sampled state, used for edge detection.
extern int  diTrigger[DI_COUNT];       ///< Edge to alert on: 0 = falling, 1 = rising.

// ----- DO channel metadata -----
extern char doName[DO_COUNT][32];      ///< Human-readable name for each digital output.
extern bool doNotifyMobile[DO_COUNT];  ///< Enable push notification for DO edge events.
extern bool doNotifyEmail[DO_COUNT];   ///< Enable e-mail notification for DO edge events.
extern bool doNotifySms[DO_COUNT];     ///< Enable SMS notification for DO edge events.
extern bool doLastState[DO_COUNT];     ///< Last sampled state, used for edge detection.
extern int  doTrigger[DO_COUNT];       ///< Edge to alert on: 0 = falling, 1 = rising.

/**
 * @brief Checks all digital inputs for edge events matching diTrigger and
 *        enqueues an AlertJob for channels that have notifications enabled.
 */
void checkDiAlerts();

/**
 * @brief Checks all digital outputs for edge events matching doTrigger and
 *        enqueues an AlertJob for channels that have notifications enabled.
 */
void checkDoAlerts();

/**
 * @brief Checks all analog inputs against their configured limits.
 *        Enqueues an AlertJob at most once every 5 minutes per channel.
 */
void checkAiLimits();

/**
 * @brief Checks all analog outputs against their configured limits.
 *        Enqueues an AlertJob at most once every 5 minutes per channel.
 */
void checkAoLimits();

/**
 * @brief FreeRTOS task that drains alertQueue and POSTs each alert to the backend.
 *
 * On a successful POST the alert is discarded. On failure (Wi-Fi down or HTTP
 * error) the alert is persisted to NVS and replayed automatically when
 * connectivity is restored (checked every 30 s). Up to 8 alerts survive
 * reboots; oldest entries are evicted when the NVS queue is full.
 *
 * @param pv Unused.
 */
void taskAlerts(void *pv);
