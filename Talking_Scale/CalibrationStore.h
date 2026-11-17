#ifndef CALIBRATION_STORE_H
#define CALIBRATION_STORE_H

#include <math.h>
#include <stdint.h>
#include <string.h>

enum class CalibrationSource : uint8_t { None, Legacy, Journal };

// EEPROM must provide read(address) and update(address, byte). The serialized
// format is identical on AVR and the host; no structure padding is stored.
template <class EEPROMType> class CalibrationStore {
public:
  explicit CalibrationStore(EEPROMType &storage) : eeprom(storage) {
    static_assert(sizeof(float) == 4, "Calibration storage requires a 32-bit float");
  }

  bool load(float &factor) {
    float candidate;
    uint32_t sequence;
    uint8_t slot;
    lastSource = CalibrationSource::None;
    if (scan(candidate, sequence, slot)) {
      // A verified record remains usable if a worn seal byte cannot be repaired.
      // save() requires the seal before accepting any changed calibration.
      seal();
      factor = candidate;
      lastSource = CalibrationSource::Journal;
      return true;
    }
    if (sealed() || !legacy(candidate)) return false;

    // An unsealed journal can contain a torn first migration. Only the legacy
    // value is written before sealing, so retrying it cannot resurrect an older
    // calibration after a later calibration has been saved.
    if (!writeRecord(0, 0, candidate) || !seal()) return false;
    factor = candidate;
    lastSource = CalibrationSource::Legacy;
    return true;
  }

  bool save(float factor) {
    if (!validFactor(factor)) return false;
    float previous;
    uint32_t sequence = 0;
    uint8_t slot = 0;
    bool found = scan(previous, sequence, slot);
    if (!found && !sealed() && legacy(previous)) {
      // Also make save() safe when the caller did not call load() first.
      if (!writeRecord(0, 0, previous) || !seal()) return false;
      found = true;
    }
    // Seal before changing a valid calibration. On an empty EEPROM this is also
    // safe: no valid legacy calibration exists to preserve before the first save.
    if (!seal()) return false;
    if (found) {
      if (previous == factor) return true; // Avoid writes for unchanged factors.
    }
    if (!writeRecord(found ? static_cast<uint8_t>(slot ^ 1) : 0,
                     found ? sequence + 1 : 0, factor)) return false;
    lastSource = CalibrationSource::Journal;
    return true;
  }

  CalibrationSource source() const { return lastSource; }

private:
  // Bytes 0..3 retain the legacy float; 4..7 are reserved for user settings.
  static const int SEAL_ADDRESS = 8;
  static const uint8_t SEAL_VALUE = 0xa5;
  static const int FIRST_SLOT = 16;
  static const uint8_t RECORD_SIZE = 16;
  static const uint8_t COMMITTED = 0xa5;
  static const uint8_t VERSION = 1;
  EEPROMType &eeprom;
  CalibrationSource lastSource = CalibrationSource::None;

  static bool validFactor(float factor) {
    return isfinite(factor) && factor != 0.0f;
  }

  static void encode32(uint8_t *destination, uint32_t value) {
    for (uint8_t i = 0; i < 4; ++i) {
      destination[i] = static_cast<uint8_t>(value >> (8 * i));
    }
  }

  static uint32_t decode32(const uint8_t *source) {
    uint32_t value = 0;
    for (uint8_t i = 0; i < 4; ++i) {
      value |= static_cast<uint32_t>(source[i]) << (8 * i);
    }
    return value;
  }

  static uint16_t checksum(const uint8_t *record) {
    uint16_t crc = 0xffff;
    for (uint8_t i = 1; i < 14; ++i) {
      crc ^= static_cast<uint16_t>(record[i]) << 8;
      for (uint8_t bit = 0; bit < 8; ++bit) {
        crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                             : static_cast<uint16_t>(crc << 1);
      }
    }
    return crc;
  }

  bool sealed() { return eeprom.read(SEAL_ADDRESS) == SEAL_VALUE; }
  bool seal() {
    eeprom.update(SEAL_ADDRESS, SEAL_VALUE);
    return sealed();
  }

  bool legacy(float &factor) {
    uint8_t bytes[4];
    for (uint8_t i = 0; i < 4; ++i) bytes[i] = eeprom.read(i);
    const uint32_t bits = decode32(bytes);
    memcpy(&factor, &bits, sizeof(factor));
    return validFactor(factor);
  }

  bool readRecord(uint8_t slot, float &factor, uint32_t &sequence) {
    uint8_t record[RECORD_SIZE];
    const int address = FIRST_SLOT + slot * RECORD_SIZE;
    for (uint8_t i = 0; i < RECORD_SIZE; ++i) record[i] = eeprom.read(address + i);
    if (record[0] != COMMITTED || record[1] != VERSION ||
        record[10] != 0xca || record[11] != 0x1b ||
        record[12] != 0 || record[13] != 0) return false;
    const uint16_t stored = static_cast<uint16_t>(record[14]) |
                            static_cast<uint16_t>(record[15]) << 8;
    if (stored != checksum(record)) return false;
    const uint32_t bits = decode32(record + 6);
    memcpy(&factor, &bits, sizeof(factor));
    if (!validFactor(factor)) return false;
    sequence = decode32(record + 2);
    return true;
  }

  bool scan(float &factor, uint32_t &sequence, uint8_t &slot) {
    float otherFactor;
    uint32_t otherSequence;
    const bool first = readRecord(0, factor, sequence);
    const bool second = readRecord(1, otherFactor, otherSequence);
    const uint32_t difference = second && first ? otherSequence - sequence : 0;
    if (second && (!first || (difference != 0 && difference < 0x80000000UL))) {
      factor = otherFactor;
      sequence = otherSequence;
      slot = 1;
    } else {
      slot = 0;
    }
    return first || second;
  }

  bool writeRecord(uint8_t slot, uint32_t sequence, float factor) {
    uint8_t record[RECORD_SIZE] = {};
    record[0] = COMMITTED;
    record[1] = VERSION;
    encode32(record + 2, sequence);
    uint32_t bits;
    memcpy(&bits, &factor, sizeof(bits));
    encode32(record + 6, bits);
    record[10] = 0xca;
    record[11] = 0x1b;
    const uint16_t crc = checksum(record);
    record[14] = static_cast<uint8_t>(crc);
    record[15] = static_cast<uint8_t>(crc >> 8);
    const int address = FIRST_SLOT + slot * RECORD_SIZE;
    eeprom.update(address, 0); // Invalidate the target before changing its bytes.
    if (eeprom.read(address) != 0) return false;
    for (uint8_t i = 1; i < RECORD_SIZE; ++i) eeprom.update(address + i, record[i]);
    for (uint8_t i = 1; i < RECORD_SIZE; ++i) {
      if (eeprom.read(address + i) != record[i]) return false;
    }
    eeprom.update(address, COMMITTED); // Commit only after the payload and CRC.
    float verifiedFactor;
    uint32_t verifiedSequence;
    return readRecord(slot, verifiedFactor, verifiedSequence) &&
           verifiedSequence == sequence && verifiedFactor == factor;
  }
};

#endif
