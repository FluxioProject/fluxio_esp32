#pragma once
#include "app_state.h"
#include <Arduino.h>

#ifndef IO_SIMULATION

/**
 * @brief Asserts a condition inside a test case.
 *
 * Prints "PASS" or "FAIL (line N)" to Serial and updates the pass/fail counters.
 *
 * @param expr Boolean expression to evaluate.
 * @param msg  Short description shown in the test output.
 */
#define TEST_ASSERT(expr, msg) _testAssert((expr), (msg), __LINE__)

/** @brief Prints the command help menu and runs all unit tests once at startup. */
void debugSerialInit();

/**
 * @brief Processes interactive Serial commands and periodically prints the IO state.
 *
 * Must be called from loop(). Prints IO state every 5 s and handles commands:
 * test, print, help, do, di, ao, ai.
 */
void debugSerialLoop();

/** @brief Dumps the current values of all AI, AO, DI, and DO channels to Serial. */
void serialPrintIO();

/** @brief Runs the full suite of hardware unit tests and prints a pass/fail summary. */
void runAllTests();

/** @internal Do not call directly — use the TEST_ASSERT macro instead. */
void _testAssert(bool expr, const char *msg, int line);

#endif
