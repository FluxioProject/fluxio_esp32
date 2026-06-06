#include "hal.h"

HAL hal;

void HAL::init()
{
#ifndef IO_SIMULATION
    for (int i = 0; i < DI_COUNT; i++)
        if (PIN_DI[i] >= 0) pinMode(PIN_DI[i], INPUT_PULLDOWN);

    for (int i = 0; i < DO_COUNT; i++)
        if (PIN_DO[i] >= 0) pinMode(PIN_DO[i], OUTPUT);

    for (int i = 0; i < AO_COUNT; i++) {
        if (PIN_AO[i] < 0) continue;
        ledcSetup(PWM_CH[i], PWM_FREQ, PWM_RES);
        ledcAttachPin(PIN_AO[i], PWM_CH[i]);
    }
#endif
}

float HAL::mapf(float x, float in_min, float in_max,
                float out_min, float out_max)
{
    if (fabsf(in_max - in_min) < 1e-6f)
        return out_min;
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

void HAL::updateIO()
{
#ifdef IO_SIMULATION
    for (int i = 0; i < AI_COUNT; i++)
        ai[i] = random(0, 1000) / 10.0f;

    for (int i = 0; i < DI_COUNT; i++)
        di[i] = random(0, 2);
#else
    // =====================================================
    // ANALOG INPUT (AI) — 4–20 mA → Engineering
    // =====================================================
    for (int i = 0; i < AI_COUNT; i++) {
        if (PIN_AI[i] < 0) continue;

        int raw = analogRead(PIN_AI[i]);

        float vpin = (raw * VREF) / ADC_MAX;
        float imA  = (vpin * 1000.0f) / SHUNT_OHMS;

        float eng = mapf(imA, 4.0f, 20.0f, aiMapMin[i], aiMapMax[i]);

        // Safety clamp
        if (eng < aiMapMin[i]) eng = aiMapMin[i];
        if (eng > aiMapMax[i]) eng = aiMapMax[i];

        ai[i] = eng;
    }

    // =====================================================
    // DIGITAL INPUT
    // =====================================================
    for (int i = 0; i < DI_COUNT; i++)
        if (PIN_DI[i] >= 0) di[i] = digitalRead(PIN_DI[i]);

    // =====================================================
    // DIGITAL OUTPUT
    // =====================================================
    for (int i = 0; i < DO_COUNT; i++)
        if (PIN_DO[i] >= 0) digitalWrite(PIN_DO[i], doo[i] ? HIGH : LOW);

    // =====================================================
    // ANALOG OUTPUT (AO) — Engineering → 4–20 mA
    // =====================================================
    for (int i = 0; i < AO_COUNT; i++) {
        if (PIN_AO[i] < 0) continue;

        float eng = ao[i];

        // Clamp to configured range
        if (eng < aoMapMin[i]) eng = aoMapMin[i];
        if (eng > aoMapMax[i]) eng = aoMapMax[i];

        // Engineering → current → voltage → PWM
        float imA  = mapf(eng, aoMapMin[i], aoMapMax[i], 4.0f, 20.0f);
        float vout = (imA / 1000.0f) * SHUNT_OHMS;

        int maxDuty = (1 << PWM_RES) - 1;
        int duty    = (int)((vout / VREF) * maxDuty);

        if (duty < 0)       duty = 0;
        if (duty > maxDuty) duty = maxDuty;

        ledcWrite(PWM_CH[i], duty);
    }
#endif
}

