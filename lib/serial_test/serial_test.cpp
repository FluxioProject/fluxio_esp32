#include "serial_test.h"
#include <hal.h>

#ifndef IO_SIMULATION

static int s_passed = 0;
static int s_failed = 0;

static void testBegin(const char *name) {
  Serial.printf("\n[TEST] %s\n", name);
}

void _testAssert(bool expr, const char *msg, int line) {
  if (expr) {
    Serial.printf("  %-44s PASS\n", msg);
    s_passed++;
  } else {
    Serial.printf("  %-44s FAIL (line %d)\n", msg, line);
    s_failed++;
  }
}

static void testDOWriteReadback() {
  testBegin("Digital Output write-readback");
  hal.doo[0] = 1; hal.updateIO();
  TEST_ASSERT(hal.doo[0] == 1, "DO[0]: write 1, readback == 1");
  hal.doo[0] = 0; hal.updateIO();
  TEST_ASSERT(hal.doo[0] == 0, "DO[0]: write 0, readback == 0");
}

static void testDIReadBinary() {
  testBegin("Digital Input returns binary value");
  hal.updateIO();
  TEST_ASSERT(hal.di[0] == 0 || hal.di[0] == 1, "DI[0] is 0 or 1");
}

static void testAIReadRange() {
  testBegin("Analog Input within configured mapped range");
  hal.updateIO();
  TEST_ASSERT(hal.ai[0] >= hal.aiMapMin[0] && hal.ai[0] <= hal.aiMapMax[0],
              "AI[0] within [mapMin, mapMax]");
}

static void testAOExtremeValues() {
  testBegin("Analog Output extreme values do not crash");
  float prev = hal.ao[0];
  hal.ao[0] = -9999.0f; hal.updateIO();
  TEST_ASSERT(true, "AO[0] = -9999: updateIO did not crash");
  hal.ao[0] = 9999.0f; hal.updateIO();
  TEST_ASSERT(true, "AO[0] = +9999: updateIO did not crash");
  hal.ao[0] = prev; hal.updateIO();
}

void runAllTests() {
  s_passed = 0;
  s_failed = 0;
  Serial.println("\n========================================");
  Serial.println("           UNIT TESTS");
  Serial.println("========================================");
  testDOWriteReadback();
  testDIReadBinary();
  testAIReadRange();
  testAOExtremeValues();
  Serial.println("\n----------------------------------------");
  Serial.printf("  RESULT: %d passed, %d failed\n", s_passed, s_failed);
  Serial.println("----------------------------------------\n");
}

void serialPrintIO() {
  Serial.println("---- IO STATE ----");
  for (int i = 0; i < AI_COUNT; i++) Serial.printf("  AI[%d] = %.2f\n", i, hal.ai[i]);
  for (int i = 0; i < AO_COUNT; i++) Serial.printf("  AO[%d] = %.2f\n", i, hal.ao[i]);
  for (int i = 0; i < DI_COUNT; i++) Serial.printf("  DI[%d] = %d\n",   i, hal.di[i]);
  for (int i = 0; i < DO_COUNT; i++) Serial.printf("  DO[%d] = %d\n",   i, hal.doo[i]);
  Serial.println("------------------");
}

static void printHelp() {
  Serial.println("\n=== Debug Console ===");
  Serial.println("  test            run all unit tests");
  Serial.println("  print           print current IO state");
  Serial.println("  do <idx> <0|1>  set digital output");
  Serial.println("  ao <idx> <val>  set analog output");
  Serial.println("  di <idx> <0|1>  override digital input");
  Serial.println("  ai <idx> <val>  override analog input");
  Serial.println("  help            show this menu");
  Serial.println("=====================");
}

void debugSerialInit() {
  printHelp();
  // runAllTests();
}

static unsigned long s_lastPrint = 0;

void debugSerialLoop() {
  if (millis() - s_lastPrint >= 5000) {
    s_lastPrint = millis();
    serialPrintIO();
  }

  if (!Serial.available()) return;

  String cmd = Serial.readStringUntil('\n');
  cmd.trim();

  if (cmd == "test")  { runAllTests();   return; }
  if (cmd == "print") { serialPrintIO(); return; }
  if (cmd == "help")  { printHelp();     return; }

  int idx, ival;
  float fval;

  if (sscanf(cmd.c_str(), "do %d %d", &idx, &ival) == 2 && idx >= 0 && idx < DO_COUNT) {
    hal.doo[idx] = ival ? 1 : 0;
    hal.updateIO();
    Serial.printf("DO[%d] = %d\n", idx, hal.doo[idx]);
    return;
  }
  if (sscanf(cmd.c_str(), "di %d %d", &idx, &ival) == 2 && idx >= 0 && idx < DI_COUNT) {
    hal.di[idx] = ival ? 1 : 0;
    Serial.printf("DI[%d] = %d\n", idx, hal.di[idx]);
    return;
  }
  if (sscanf(cmd.c_str(), "ao %d %f", &idx, &fval) == 2 && idx >= 0 && idx < AO_COUNT) {
    hal.ao[idx] = fval;
    hal.updateIO();
    Serial.printf("AO[%d] = %.2f\n", idx, hal.ao[idx]);
    return;
  }
  if (sscanf(cmd.c_str(), "ai %d %f", &idx, &fval) == 2 && idx >= 0 && idx < AI_COUNT) {
    hal.ai[idx] = fval;
    Serial.printf("AI[%d] = %.2f\n", idx, hal.ai[idx]);
    return;
  }

  Serial.println("Unknown command. Type 'help' for available commands.");
}

#endif
