#ifndef EEPROM_RESET_H
#define EEPROM_RESET_H

#include <stdint.h>

enum class EEPROMResetResult : uint8_t { Busy, Complete, Failed };

// Storage supplies read(address), update(address, byte), and length(). Each
// step erases at most one byte so callers can continue polling their controls.
template<class Storage> class EEPROMReset {
public:
  explicit EEPROMReset(Storage &storage) : eeprom(storage) {}

  bool pending() const {
    return eeprom.length() > MARKER_ADDRESS &&
           eeprom.read(MARKER_ADDRESS) == RESET_MARKER;
  }

  bool begin() {
    nextAddress = 0;
    active = false;
    failed = false;
    if (eeprom.length() <= MARKER_ADDRESS) {
      failed = true;
      return false;
    }
    if (!pending()) eeprom.update(MARKER_ADDRESS, RESET_MARKER);
    if (!pending()) {
      failed = true;
      return false;
    }
    active = true;
    return true;
  }

  EEPROMResetResult step() {
    if (failed) return EEPROMResetResult::Failed;
    if (!active) {
      if (!pending()) return EEPROMResetResult::Complete;
      // A fresh instance resumes a reset interrupted by loss of power.
      nextAddress = 0;
      active = true;
    }
    if (!pending()) return fail();

    if (nextAddress == MARKER_ADDRESS) ++nextAddress;
    const int address = nextAddress < eeprom.length() ? nextAddress : MARKER_ADDRESS;
    if (eeprom.read(address) != ERASED) {
      eeprom.update(address, ERASED);
      if (eeprom.read(address) != ERASED) return fail();
    }
    if (address == MARKER_ADDRESS) {
      // Clearing the marker is the last write, after every other byte verifies.
      active = false;
      return EEPROMResetResult::Complete;
    }
    ++nextAddress;
    return EEPROMResetResult::Busy;
  }

private:
  static const int MARKER_ADDRESS = 9;
  static const uint8_t RESET_MARKER = 0x52;
  static const uint8_t ERASED = 0xff;
  Storage &eeprom;
  int nextAddress = 0;
  bool active = false;
  bool failed = false;

  EEPROMResetResult fail() {
    failed = true;
    return EEPROMResetResult::Failed;
  }
};

#endif
