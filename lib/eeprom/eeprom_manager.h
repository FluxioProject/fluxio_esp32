#pragma once

#include <Arduino.h>
#include <EEPROM.h>
#include <app_state.h>

/**
 * @brief Wrapper around the ESP32 EEPROM library for persistent parameter storage.
 *
 * Handles reading and writing Wi-Fi credentials and arbitrary floats / strings
 * at fixed offsets defined in hw_config.h.
 */
class EEPROMManager {
public:
  EEPROMManager() = default;

  /** @brief Initialises the EEPROM peripheral. Restarts the device on failure. */
  void setupEEPROM();

  /** @brief Persists the current Wi-Fi SSID and password to EEPROM. */
  void saveParamsEEPROM();

  /**
   * @brief Reads Wi-Fi credentials from EEPROM into the global ssid / password
   *        variables, then restores the saved logic program from flash.
   */
  void readParamsEEPROM();

  /**
   * @brief Writes a 4-byte float to EEPROM at the given address.
   * @param startAddress Byte offset within EEPROM.
   * @param value        Value to store.
   */
  void saveFloatEEPROM(int startAddress, float value);

  /**
   * @brief Reads a 4-byte float from EEPROM.
   * @param startAddress Byte offset within EEPROM.
   * @return Stored float value.
   */
  float readFloatEEPROM(int startAddress);

  /**
   * @brief Writes a null-padded string to EEPROM.
   * @param startAddress Byte offset within EEPROM.
   * @param str          String to store.
   * @param maxLength    Maximum number of bytes to write (including null terminator).
   */
  void saveStringEEPROM(int startAddress, String str, int maxLength);

  /**
   * @brief Reads a null-terminated string from EEPROM.
   * @param startAddress Byte offset within EEPROM.
   * @param maxLength    Maximum number of bytes to read.
   * @return Stored string (empty if the first byte is '\0').
   */
  String readStringEEPROM(int startAddress, int maxLength);
};

/** Global EEPROMManager instance. */
extern EEPROMManager eepromManager;
