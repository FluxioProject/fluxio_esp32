#include "logic.h"
#include "alerts.h"
#include <app_state.h>
#include <hal.h>


uint8_t logicBlockCount = 0;
bool logicLoaded = false;
Preferences prefs;
int blockIdToIndex[MAX_BLOCKS];
LogicBlock logicBlocks[MAX_BLOCKS];

void saveLogicToFlash(const String &json) {
  prefs.begin("logic", false);
  prefs.putString("program", json);
  prefs.end();

  Serial.println("Logic saved to flash");
}

bool loadLogicFromFlash() {
  prefs.begin("logic", true);
  String json = prefs.getString("program", "");
  prefs.end();

  if (json.length() == 0) {
    Serial.println("No logic saved to flash");
    return false;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, json);

  if (err) {
    Serial.println("Error reading logic from flash");
    return false;
  }

  logicJsonCache = json;

  if (loadLogicFromJson(doc)) {
    Serial.println("Logic restored from flash");
    return true;
  }

  return false;
}

bool loadLogicFromJson(JsonDocument &doc) {
  for (int i = 0; i < MAX_BLOCKS; i++)
    blockIdToIndex[i] = -1;

  if (!doc["blocks"].is<JsonArray>())
    return false;

  JsonArray blocks = doc["blocks"];
  logicBlockCount = 0;

  for (JsonObject b : blocks) {
    if (logicBlockCount >= MAX_BLOCKS)
      break;

    LogicBlock &lb = logicBlocks[logicBlockCount];

    lb.id = b["id"];

    if (lb.id >= MAX_BLOCKS) {
      Serial.printf("Block id=%d out of range, skipping\n", lb.id);
      continue;
    }

    lb.type = b["t"];
    lb.op = b["op"] | 0;
    lb.lastValue = 0;
    lb.timerStartMs = 0;
    lb.timerRunning = false;

    // IO
    lb.ioType = 255; // invalid by default
    lb.ioChannel = 0;

    if (lb.type == BLOCK_IO && b["io"].is<JsonArray>()) {
      lb.ioType = b["io"][0];
      lb.ioChannel = b["io"][1];
    }

    blockIdToIndex[lb.id] = logicBlockCount;

    JsonArray inputs = b["in"];
    lb.inputCount = min((uint8_t)inputs.size(), (uint8_t)MAX_INPUTS);

    for (uint8_t i = 0; i < lb.inputCount; i++) {
      JsonArray in = inputs[i];
      lb.inputs[i].kind = in[0];

      if (lb.inputs[i].kind == INPUT_CONSTANT)
        lb.inputs[i].value = in[1];
      else
        lb.inputs[i].fromBlockId = in[1];
    }

    logicBlockCount++;

    Serial.printf("Block id=%d type=%d ioType=%d ioCh=%d inputs=%d\n", lb.id,
                  lb.type, lb.ioType, lb.ioChannel, lb.inputCount);

    for (int j = 0; j < lb.inputCount; j++) {
      if (lb.inputs[j].kind == INPUT_CONSTANT) {
        Serial.printf("  input[%d] = CONST %.2f\n", j, lb.inputs[j].value);
      } else {
        Serial.printf("  input[%d] = BLOCK %d\n", j, lb.inputs[j].fromBlockId);
      }
    }
  }

  logicLoaded = true;
  Serial.printf("Total blocks loaded: %d\n", logicBlockCount);
  Serial.println("==================");
  return true;
}

float getInputValue(const BlockInput &in) {
  if (in.kind == INPUT_CONSTANT)
    return in.value;

  int idx = blockIdToIndex[in.fromBlockId];

  if (idx < 0 || idx >= logicBlockCount) {
    Serial.printf("ERROR: invalid source block id=%d idx=%d\n", in.fromBlockId,
                  idx);
    return 0;
  }
  return logicBlocks[idx].lastValue;
}

void executeLogic() {
  for (int pass = 0; pass < 3; pass++) {
    for (int i = 0; i < logicBlockCount; i++) {
      LogicBlock &b = logicBlocks[i];

      switch (b.type) {
      case BLOCK_MATH: {
        float a = getInputValue(b.inputs[0]);
        float c = getInputValue(b.inputs[1]);

        if (b.op == 0)
          b.lastValue = a + c;
        else if (b.op == 1)
          b.lastValue = a - c;
        else if (b.op == 2)
          b.lastValue = a * c;
        else if (b.op == 3)
          b.lastValue = (fabsf(c) < 1e-6f) ? 0.0f : (a / c);
        break;
      }

      case BLOCK_COMPARE: {
        float a = getInputValue(b.inputs[0]);
        float c = getInputValue(b.inputs[1]);

        if (b.op == 0) // >
          b.lastValue = (a > c) ? 1.0f : 0.0f;
        else if (b.op == 1) // <
          b.lastValue = (a < c) ? 1.0f : 0.0f;
        else if (b.op == 2) // ==
          b.lastValue = (a == c) ? 1.0f : 0.0f;
        else if (b.op == 3) // >=
          b.lastValue = (a >= c) ? 1.0f : 0.0f;
        else if (b.op == 4) // <=
          b.lastValue = (a <= c) ? 1.0f : 0.0f;

        break;
      }
      case BLOCK_IO: {
        // INPUT BLOCK (AI / DI)
        if (b.inputCount == 0) {
          switch (b.ioType) {
          case IO_AI:
            b.lastValue = (b.ioChannel < AI_COUNT) ? hal.ai[b.ioChannel] : 0;
            break;

          case IO_DI:
            b.lastValue = (b.ioChannel < DI_COUNT) ? (hal.di[b.ioChannel] ? 1.0f : 0.0f) : 0;
            break;

          default:
            b.lastValue = 0;
            break;
          }
        }
        // OUTPUT BLOCK (AO / DO)
        else {
          b.lastValue = getInputValue(b.inputs[0]);
          switch (b.ioType) {
          case IO_AO:
            if (b.ioChannel < AO_COUNT)
              hal.ao[b.ioChannel] = b.lastValue;
            break;

          case IO_DO:
            if (b.ioChannel < DO_COUNT)
              hal.doo[b.ioChannel] = (b.lastValue > 0.5f) ? 1 : 0;
            break;
          }

          hal.updateIO();
        }
        break;
      }
      case BLOCK_TIMER: {
        float in = getInputValue(b.inputs[0]);
        float delayMs = (b.inputCount > 1) ? getInputValue(b.inputs[1])
                                           : b.inputs[0].value;

        uint32_t now = millis();

        if (in > 0.5f) {
          if (!b.timerRunning) {
            b.timerRunning = true;
            b.timerStartMs = now;
            b.lastValue = 0;
          } else {
            if (now - b.timerStartMs >= (uint32_t)delayMs) {
              b.lastValue = 1.0f;
            } else {
              b.lastValue = 0.0f;
            }
          }
        } else {
          // reset when input goes low
          b.timerRunning = false;
          b.lastValue = 0.0f;
        }
        break;
      }
      }
    }
  }
}

void taskLogic(void *) {
  for (;;) {
    hal.updateIO();

    checkAiLimits();
    checkAoLimits();
    checkDiAlerts();
    checkDoAlerts();

    if (logicLoaded && !manualMode)
      executeLogic();

    vTaskDelay(pdMS_TO_TICKS(50));
  }
}
