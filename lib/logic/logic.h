#pragma once

#include <ArduinoJson.h>
#include <Preferences.h>
#include <cstdint>
#include <hw_config.h>

/** Maximum number of logic blocks in a single program. */
#define MAX_BLOCKS 64
/** Maximum number of inputs per logic block. */
#define MAX_INPUTS 2

/** @brief Identifies the computation performed by a LogicBlock. */
enum BlockType
{
  BLOCK_MATH = 0,    ///< Arithmetic: add, subtract, multiply, divide.
  BLOCK_COMPARE = 1, ///< Comparison: >, <, ==, >=, <=. Output is 0.0 or 1.0.
  BLOCK_TIMER = 2,   ///< On-delay timer: output goes high after input is high for N ms.
  BLOCK_IO = 3       ///< Reads from AI/DI or writes to AO/DO.
};

/** @brief Determines where a block gets its input value from. */
enum InputKind
{
  INPUT_CONSTANT = 0,
  INPUT_BLOCK = 1
};

/** @brief Physical IO type used by BLOCK_IO nodes. */
enum IOType
{
  IO_AI = 0, ///< Analog input read.
  IO_DI = 1, ///< Digital input read.
  IO_AO = 2, ///< Analog output write.
  IO_DO = 3  ///< Digital output write.
};

/**
 * @brief One input slot for a LogicBlock.
 *
 * Either holds a literal constant or references the lastValue of another block.
 */
struct BlockInput
{
  uint8_t kind;    ///< INPUT_CONSTANT or INPUT_BLOCK.
  float value;     ///< Literal value (used when kind == INPUT_CONSTANT).
  int fromBlockId; ///< Source block ID (used when kind == INPUT_BLOCK).
};

/**
 * @brief Represents a single node in the logic program graph.
 *
 * Loaded from JSON by loadLogicFromJson(). Evaluated in index order
 * across 3 passes per cycle by executeLogic().
 */
struct LogicBlock
{
  uint8_t id;
  uint8_t type; ///< BlockType value.
  uint8_t op;   ///< Operation code (meaning depends on type).

  uint8_t ioType;    ///< IOType value: IO_AI, IO_DI, IO_AO, or IO_DO (BLOCK_IO only). 255 = invalid.
  uint8_t ioChannel; ///< Zero-based physical channel index (BLOCK_IO only).

  uint8_t inputCount;
  BlockInput inputs[MAX_INPUTS];

  float lastValue;       ///< Result of the last evaluation; used as input by dependent blocks.
  uint32_t timerStartMs; ///< millis() when the timer input last went high (BLOCK_TIMER only).
  bool timerRunning;     ///< True while the timer input is held high (BLOCK_TIMER only).
};

/** Maps block ID → index in logicBlocks[]. -1 if the ID is not present. */
extern int blockIdToIndex[MAX_BLOCKS];
/** Flat array of all loaded blocks. Valid indices are [0, logicBlockCount). */
extern LogicBlock logicBlocks[MAX_BLOCKS];

/** Number of blocks currently loaded. */
extern uint8_t logicBlockCount;
/** True after a program has been successfully loaded. */
extern bool logicLoaded;

/** Preferences instance used to persist the logic program JSON to NVS flash. */
extern Preferences prefs;

extern bool doOutputDriven[DO_COUNT];
extern bool aoOutputDriven[AO_COUNT];

/**
 * @brief Serialises the raw JSON string of the current logic program to NVS flash.
 * @param json The complete JSON program string received via MQTT.
 */
void saveLogicToFlash(const String &json);

/**
 * @brief Reads the logic program JSON from NVS flash and loads it.
 * @return true if a valid program was found and loaded, false otherwise.
 */
bool loadLogicFromFlash();

/**
 * @brief Parses a JSON document and populates the logicBlocks array.
 *
 * Also updates logicJsonCache with the raw JSON string and sets logicLoaded = true.
 *
 * @param doc Deserialised JSON document containing a "blocks" array.
 * @return true on success, false if the "blocks" key is missing or malformed.
 */
bool loadLogicFromJson(JsonDocument &doc);

/**
 * @brief Evaluates all loaded logic blocks for one cycle.
 *
 * Runs 3 forward passes to resolve dependencies between blocks.
 * Writes computed values to hal.ao[] and hal.doo[] for BLOCK_IO output nodes.
 */
void executeLogic();

/**
 * @brief FreeRTOS task that drives the IO update, alert checks, and logic execution.
 *
 * Calls hal.updateIO(), all four alert check functions, and executeLogic()
 * (unless manualMode is true) every 50 ms.
 *
 * @param pv Unused.
 */
void taskLogic(void *pv);

extern volatile bool logicSyncRequested; // Set to true by syncLogicFromBackend(), checked in the main loop to trigger a logic sync from the backend.

/**
 * @brief Requests a logic sync from the backend.
 *
 * Sets logicSyncRequested = true, which will be checked in the main loop.
 * The backend will then be queried for the latest logic program JSON.
 */
void syncLogicFromBackend();
