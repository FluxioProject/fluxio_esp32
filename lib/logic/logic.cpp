#include "logic.h"
#include "alerts.h"
#include <app_state.h>
#include <hal.h>
#include <mqtt.h>
#include <backend.h>

uint8_t logicBlockCount = 0;
bool logicLoaded = false;
Preferences prefs;
int blockIdToIndex[MAX_BLOCKS];
LogicBlock logicBlocks[MAX_BLOCKS];
bool doOutputDriven[DO_COUNT];
bool aoOutputDriven[AO_COUNT];

void saveLogicToFlash(const String &json)
{
  prefs.begin("logic", false);
  prefs.putString("program", json);
  prefs.end();

  Serial.println("Logic saved to flash");
}

bool loadLogicFromFlash()
{
  prefs.begin("logic", true);
  String json = prefs.getString("program", "");
  prefs.end();

  if (json.length() == 0)
  {
    Serial.println("No logic saved to flash");
    return false;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, json);

  if (err)
  {
    Serial.println("Error reading logic from flash");
    return false;
  }

  logicJsonCache = json;

  if (loadLogicFromJson(doc))
  {
    Serial.println("Logic restored from flash");
    return true;
  }

  return false;
}

bool loadLogicFromJson(JsonDocument &doc)
{
  for (int i = 0; i < MAX_BLOCKS; i++)
    blockIdToIndex[i] = -1;

  for (int i = 0; i < DO_COUNT; i++)
    doOutputDriven[i] = false;
  for (int i = 0; i < AO_COUNT; i++)
    aoOutputDriven[i] = false;

  if (!doc["blocks"].is<JsonArray>())
    return false;

  JsonArray blocks = doc["blocks"];
  logicBlockCount = 0;

  for (JsonObject b : blocks)
  {
    if (logicBlockCount >= MAX_BLOCKS)
      break;

    LogicBlock &lb = logicBlocks[logicBlockCount];

    lb.id = b["id"];

    if (lb.id >= MAX_BLOCKS)
    {
      Serial.printf("Block id=%d out of range, skipping\n", lb.id);
      continue;
    }

    lb.type = b["t"];
    lb.op = b["op"] | 0;
    lb.lastValue = 0;
    lb.timerStartMs = 0;
    lb.timerRunning = false;

    // PID fields: always reset to defaults on every load, regardless of
    // block type. This slot in logicBlocks[] may have held a different
    // block type (or a different PID config) before this reload, and a
    // stale kp/ki/kd/integral left over from a previous program would be
    // silently wrong if this ever becomes a PID block later without an
    // explicit "pid" object in the JSON.
    lb.pidKp = 0.0f;
    lb.pidKi = 0.0f;
    lb.pidKd = 0.0f;
    lb.pidOutMin = 0.0f;
    lb.pidOutMax = 100.0f;
    lb.pidIntegral = 0.0f;
    lb.pidPrevError = 0.0f;
    lb.pidLastRunMs = 0;

    if (lb.type == BLOCK_PID && b["pid"].is<JsonObject>())
    {
      JsonObject pidCfg = b["pid"];
      lb.pidKp = pidCfg["kp"] | 0.0f;
      lb.pidKi = pidCfg["ki"] | 0.0f;
      lb.pidKd = pidCfg["kd"] | 0.0f;
      lb.pidOutMin = pidCfg["outMin"] | 0.0f;
      lb.pidOutMax = pidCfg["outMax"] | 100.0f;
    }

    // IO
    lb.ioType = 255; // invalid by default
    lb.ioChannel = 0;

    if (lb.type == BLOCK_IO && b["io"].is<JsonArray>())
    {
      lb.ioType = b["io"][0];
      lb.ioChannel = b["io"][1];

      // Marca esse canal como "pertence ao programa atual" quando for
      // um bloco de SAÍDA (tem input) — blocos de entrada (AI/DI) não
      // contam, já que não escrevem em hal.ao/hal.doo.
      JsonArray inputsCheck = b["in"];
      bool isOutputBlock = inputsCheck.size() > 0;

      if (isOutputBlock)
      {
        if (lb.ioType == IO_DO && lb.ioChannel < DO_COUNT)
          doOutputDriven[lb.ioChannel] = true;
        else if (lb.ioType == IO_AO && lb.ioChannel < AO_COUNT)
          aoOutputDriven[lb.ioChannel] = true;
      }
    }

    blockIdToIndex[lb.id] = logicBlockCount;

    JsonArray inputs = b["in"];
    lb.inputCount = min((uint8_t)inputs.size(), (uint8_t)MAX_INPUTS);

    for (uint8_t i = 0; i < lb.inputCount; i++)
    {
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

    if (lb.type == BLOCK_PID)
    {
      Serial.printf("  pid kp=%.4f ki=%.4f kd=%.4f outMin=%.2f outMax=%.2f\n",
                    lb.pidKp, lb.pidKi, lb.pidKd, lb.pidOutMin, lb.pidOutMax);
    }

    for (int j = 0; j < lb.inputCount; j++)
    {
      if (lb.inputs[j].kind == INPUT_CONSTANT)
      {
        Serial.printf("  input[%d] = CONST %.2f\n", j, lb.inputs[j].value);
      }
      else
      {
        Serial.printf("  input[%d] = BLOCK %d\n", j, lb.inputs[j].fromBlockId);
      }
    }
  }

  // -----------------------------------------------------------------
  // Validate cross-references BEFORE accepting the program.
  // A block that references a fromBlockId which was never defined
  // (or was skipped above for being out of range) would otherwise
  // fail silently on every executeLogic() scan, forever, at 20 Hz.
  // Reject the whole program instead of loading a broken one.
  // -----------------------------------------------------------------
  for (int i = 0; i < logicBlockCount; i++)
  {
    LogicBlock &lb = logicBlocks[i];
    for (int j = 0; j < lb.inputCount; j++)
    {
      BlockInput &in = lb.inputs[j];
      if (in.kind != INPUT_CONSTANT && blockIdToIndex[in.fromBlockId] < 0)
      {
        Serial.printf(
            "Invalid logic program: block id=%d input[%d] references "
            "missing block id=%d — program rejected\n",
            lb.id, j, in.fromBlockId);
        logicBlockCount = 0;
        logicLoaded = false;
        return false;
      }
    }
  }

  logicLoaded = true;
  Serial.printf("Total blocks loaded: %d\n", logicBlockCount);
  bool anyOrphanReset = false;
  for (int i = 0; i < DO_COUNT; i++)
  {
    if (!doOutputDriven[i] && hal.doo[i] != 0)
    {
      Serial.printf("[Logic] DO[%d] não pertence mais ao programa atual — resetando para 0\n", i);
      hal.doo[i] = 0;
      anyOrphanReset = true;
    }
  }
  for (int i = 0; i < AO_COUNT; i++)
  {
    if (!aoOutputDriven[i] && hal.ao[i] != 0)
    {
      Serial.printf("[Logic] AO[%d] não pertence mais ao programa atual — resetando para 0\n", i);
      hal.ao[i] = 0;
      anyOrphanReset = true;
    }
  }
  if (anyOrphanReset)
    hal.updateIO();

  Serial.println("==================");
  return true;
}

float getInputValue(const BlockInput &in)
{
  if (in.kind == INPUT_CONSTANT)
    return in.value;

  int idx = blockIdToIndex[in.fromBlockId];

  if (idx < 0 || idx >= logicBlockCount)
  {
    // Rate-limited safety net: loadLogicFromJson() now rejects programs
    // with dangling references up front, so this path should be
    // unreachable in normal operation. Kept as a guard against any
    // future/edge case so a bad reference can't flood the serial log.
    static uint32_t lastWarnMs = 0;
    uint32_t now = millis();
    if (now - lastWarnMs > 5000)
    {
      Serial.printf("ERROR: invalid source block id=%d idx=%d\n",
                    in.fromBlockId, idx);
      lastWarnMs = now;
    }
    return 0;
  }
  return logicBlocks[idx].lastValue;
}

void executeLogic()
{
  for (int pass = 0; pass < 3; pass++)
  {
    for (int i = 0; i < logicBlockCount; i++)
    {
      LogicBlock &b = logicBlocks[i];

      switch (b.type)
      {
      case BLOCK_MATH:
      {
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

      case BLOCK_COMPARE:
      {
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
      case BLOCK_IO:
      {
        // INPUT BLOCK (AI / DI)
        if (b.inputCount == 0)
        {
          switch (b.ioType)
          {
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
        else
        {
          b.lastValue = getInputValue(b.inputs[0]);
          switch (b.ioType)
          {
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
      case BLOCK_TIMER:
      {
        float in = getInputValue(b.inputs[0]);
        float delayMs = (b.inputCount > 1) ? getInputValue(b.inputs[1])
                                           : b.inputs[0].value;

        uint32_t now = millis();

        if (in > 0.5f)
        {
          if (!b.timerRunning)
          {
            b.timerRunning = true;
            b.timerStartMs = now;
            b.lastValue = 0;
          }
          else
          {
            if (now - b.timerStartMs >= (uint32_t)delayMs)
            {
              b.lastValue = 1.0f;
            }
            else
            {
              b.lastValue = 0.0f;
            }
          }
        }
        else
        {
          // reset when input goes low
          b.timerRunning = false;
          b.lastValue = 0.0f;
        }
        break;
      }
      case BLOCK_PID:
      {
        float enable = getInputValue(b.inputs[0]);

        if (enable <= 0.5f)
        {
          // Disabled: force output to 0 and reset internal state, so a
          // later re-enable starts from a clean slate instead of
          // resuming a stale integral/derivative from before it was
          // turned off. Runs on every pass so the disabled output (0)
          // is immediately consistent for anything reading it this
          // same executeLogic() call.
          b.pidIntegral = 0.0f;
          b.pidPrevError = 0.0f;
          b.pidLastRunMs = 0;
          b.lastValue = 0.0f;
          break;
        }

        // The integral/derivative state must only be updated ONCE per
        // executeLogic() call, not once per pass — executeLogic() runs
        // 3 passes per 50ms cycle to let cross-block references settle
        // (see the loop above), but a stateful accumulator like the PID
        // integral would otherwise accumulate 3x every cycle if it ran
        // on every pass, throwing off the tuning by a factor that
        // depends on how many passes had already converged. Gating on
        // pass == 0 keeps the state update to exactly once per cycle.
        // (b.lastValue itself is only (re)computed here too — later
        // passes just keep reading the value computed here, same as
        // BLOCK_TIMER does for its own state.)
        if (pass == 0)
        {
          float pv = getInputValue(b.inputs[1]);
          float sp = getInputValue(b.inputs[2]);

          uint32_t now = millis();
          // On the very first run (pidLastRunMs == 0, e.g. right after
          // load or after being re-enabled), fall back to the nominal
          // scan period instead of computing dt against a stale/zero
          // timestamp.
          float dt = (b.pidLastRunMs == 0) ? 0.05f
                                            : (now - b.pidLastRunMs) / 1000.0f;
          b.pidLastRunMs = now;

          float error = sp - pv;

          // Tentative integral update — only committed below if it
          // doesn't push the (unclamped) output past outMin/outMax.
          float tentativeIntegral = b.pidIntegral + error * dt;

          float derivative =
              (dt > 1e-6f) ? (error - b.pidPrevError) / dt : 0.0f;
          b.pidPrevError = error;

          float unclampedOutput = b.pidKp * error +
                                   b.pidKi * tentativeIntegral +
                                   b.pidKd * derivative;

          // -----------------------------------------------------------
          // Anti-windup (conditional integration / clamping method):
          // only let the integral accumulate if the resulting output
          // would actually stay within [outMin, outMax]. If it would
          // saturate, freeze the integral at its previous value instead
          // of committing the tentative one.
          //
          // This is what keeps a noisy/spiky input from "winding up"
          // the integral term far past what's needed to saturate the
          // output — without it, a single spike could push the integral
          // way past outMax, and the controller would then keep the
          // output pinned high (or low) for a long time afterwards,
          // even once the input settles back to normal, until the
          // integral slowly unwinds. Freezing it here means the output
          // comes back down as soon as the spike is gone, instead of
          // lagging behind it.
          // -----------------------------------------------------------
          float output = unclampedOutput;
          if (output > b.pidOutMax)
            output = b.pidOutMax;
          else if (output < b.pidOutMin)
            output = b.pidOutMin;

          if (unclampedOutput <= b.pidOutMax && unclampedOutput >= b.pidOutMin)
            b.pidIntegral = tentativeIntegral;
          // else: leave b.pidIntegral unchanged (frozen this cycle).

          b.lastValue = output;
        }
        break;
      }
      }
    }
  }
}

void taskLogic(void *)
{
  static uint32_t lastRetryMs = 0;
  const uint32_t RETRY_INTERVAL_MS = 20000; // tenta de novo a cada 20s se ainda não sincronizou

  for (;;)
  {
    hal.updateIO();

    checkAiLimits();
    checkAoLimits();
    checkDiAlerts();
    checkDoAlerts();

    if (logicSyncRequested)
    {
      logicSyncRequested = false;
      syncLogicFromBackend();
    }

    uint32_t now = millis();
    if ((!logicSynced || !channelsSynced || !fwChecked) && (now - lastRetryMs >= RETRY_INTERVAL_MS))
    {
      lastRetryMs = now;

      if (!logicSynced)
      {
        Serial.println("[Retry] Lógica ainda não sincronizada, tentando novamente...");
        syncLogicFromBackend();
      }
      if (!channelsSynced)
      {
        Serial.println("[Retry] Canais ainda não sincronizados, tentando novamente...");
        if (fetchAllChannels())
          Serial.println("[Retry] Canais sincronizados com sucesso");
      }
      if (!fwChecked)
      {
        Serial.println("[Retry] Firmware ainda não checado, tentando novamente...");
        checkForFirmwareUpdate();
      }
    }

    if (logicLoaded && !manualMode)
      executeLogic();

    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

volatile bool logicSyncRequested = false;

// Guarda o 'updatedAt' da última sincronização bem-sucedida, só em RAM.
// Não precisa persistir: depois de um reboot, um valor vazio força uma
// nova checagem no próximo logic_sync (ou no boot, se você adicionar o
// hook opcional descrito abaixo) — o que é até desejável, garante que o
// device confirma que está com a versão mais recente após reiniciar.
static String lastSyncedUpdatedAt = "";

void syncLogicFromBackend()
{
  Serial.println("[Logic] Sincronizando lógica com o backend...");

  String json, updatedAt;

  // fetchLogicFromBackend faz o GET em
  // "<BACKEND_URL>/products/<DEVICE_ID>/logic-for-device" e devolve o
  // corpo (json) e o campo updatedAt já extraído — ver backend.h/.cpp
  // (mesma dupla de responsabilidades que fetchMQTTCredentials já usa).
  if (!fetchLogicFromBackend(json, updatedAt))
  {
    Serial.println("[Logic] Falha ao buscar lógica no backend");
    return;
  }

  if (updatedAt.length() > 0 && updatedAt == lastSyncedUpdatedAt)
  {
    Serial.println("[Logic] Lógica do backend sem mudanças, ignorando");
    logicSynced = true;
    return;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, json);

  if (err)
  {
    Serial.printf("[Logic] JSON inválido vindo do backend: %s\n", err.c_str());
    return;
  }

  if (loadLogicFromJson(doc))
  {
    logicJsonCache = json;
    saveLogicToFlash(json);
    lastSyncedUpdatedAt = updatedAt;
    logicSynced = true;

    Serial.println("[Logic] Lógica atualizada a partir do backend e persistida");

    mqtt.publish(topicLogic, logicJsonCache.c_str(), true);
  }
  else
  {
    Serial.println("[Logic] Programa do backend rejeitado (inválido)");
  }
}