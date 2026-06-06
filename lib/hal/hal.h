#pragma once

#include <Arduino.h>
#include <hw_config.h>

/**
 * @brief Hardware Abstraction Layer for all physical I/O channels.
 *
 * All IO arrays are public so that higher-level modules (logic, mqtt,
 * alerts, serial_test) can read and write them directly. Call updateIO()
 * to flush output values to hardware and refresh input readings.
 */
class HAL
{
public:
    // ----- IO values — read/written by logic, mqtt, alerts, serial_test -----
    float ai[AI_COUNT]  = {0}; ///< Analog inputs in engineering units (mapped from 4–20 mA).
    float ao[AO_COUNT]  = {0}; ///< Analog outputs in engineering units (mapped to 4–20 mA).
    int   di[DI_COUNT]  = {0}; ///< Digital inputs (0 or 1).
    int   doo[DO_COUNT] = {0}; ///< Digital outputs (0 or 1).

#ifndef IO_SIMULATION
    // ----- Mapping ranges — populated by backend.cpp after fetchAllChannels() -----
    float aiMapMin[AI_COUNT] = {0};   ///< Engineering minimum for each AI channel.
    float aiMapMax[AI_COUNT] = {100}; ///< Engineering maximum for each AI channel.
    float aoMapMin[AO_COUNT] = {0};   ///< Engineering minimum for each AO channel.
    float aoMapMax[AO_COUNT] = {100}; ///< Engineering maximum for each AO channel.
#endif

    /**
     * @brief Configures GPIO pins and peripherals (PWM, ADC).
     *
     * Must be called once in setup() before the first updateIO().
     * Has no effect when IO_SIMULATION is defined.
     */
    void init();

    /**
     * @brief Reads all physical inputs into the IO arrays and writes all
     *        output arrays to the corresponding hardware peripherals.
     *
     * AI: ADC raw → mA → engineering units via aiMapMin/aiMapMax.
     * AO: engineering units → mA → voltage → PWM duty cycle.
     * DI: digitalRead(); DO: digitalWrite().
     * In simulation mode, AI and DI are filled with random values.
     */
    void updateIO();

private:
    /**
     * @brief Floating-point linear interpolation equivalent of Arduino map().
     * @return Mapped value; returns out_min when in_max == in_min.
     */
    static float mapf(float x, float in_min, float in_max,
                      float out_min, float out_max);
};

/** Global HAL instance — include hal.h and use `hal.ai[i]`, `hal.doo[i]`, etc. */
extern HAL hal;
