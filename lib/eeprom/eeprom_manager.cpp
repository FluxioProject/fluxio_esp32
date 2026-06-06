#include "eeprom_manager.h"
#include <logic.h>

EEPROMManager eepromManager;

void EEPROMManager::setupEEPROM() {
  if (!EEPROM.begin(EEPROM_SIZE))
    ESP.restart();
}

void EEPROMManager::saveStringEEPROM(int startAddress, String str,
                                     int maxLength) {
  int length = str.length();

  if (startAddress + maxLength > EEPROM_SIZE)
    return;

  for (int i = 0; i < maxLength; i++) {
    if (i < length) {
      EEPROM.write(startAddress + i, str[i]);
    } else {
      EEPROM.write(startAddress + i, '\0');
    }
  }
  EEPROM.commit();
}

String EEPROMManager::readStringEEPROM(int startAddress, int maxLength) {
  String str = "";

  for (int i = 0; i < maxLength; i++) {
    char c = EEPROM.read(startAddress + i);
    if (c == '\0') {
      break;
    }
    str += c;
  }
  return str;
}

void EEPROMManager::saveFloatEEPROM(int startAddress, float value) {
  EEPROM.put(startAddress, value);
  EEPROM.commit();
}

float EEPROMManager::readFloatEEPROM(int startAddress) {
  float value;
  EEPROM.get(startAddress, value);
  return value;
}

void EEPROMManager::saveParamsEEPROM() {
  saveStringEEPROM(SSID_ADDRESS, ssid, SSID_MAX_LENGTH);
  saveStringEEPROM(PASSWORD_ADDRESS, password, PASSWORD_MAX_LENGTH);
}

void EEPROMManager::readParamsEEPROM() {
  ssid = readStringEEPROM(SSID_ADDRESS, SSID_MAX_LENGTH);
  password = readStringEEPROM(PASSWORD_ADDRESS, PASSWORD_MAX_LENGTH);

  loadLogicFromFlash();
}