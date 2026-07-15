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