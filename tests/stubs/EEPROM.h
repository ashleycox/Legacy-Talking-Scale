#ifndef TEST_EEPROM_H
#define TEST_EEPROM_H

#include <array>
#include <cassert>
#include <cstring>
#include <cstdint>

class TestEEPROM {
public:
  std::array<uint8_t, 64> bytes;
  unsigned int putCalls = 0;
  unsigned int changedBytes = 0;

  TestEEPROM() { bytes.fill(0xff); }

  uint8_t read(int address) {
    assert(address >= 0 && static_cast<size_t>(address) < bytes.size());
    return bytes[address];
  }
  void update(int address, uint8_t value) {
    assert(address >= 0 && static_cast<size_t>(address) < bytes.size());
    if (bytes[address] != value) { bytes[address] = value; ++changedBytes; }
  }

  template <typename T> T &get(int address, T &value) {
    assert(address >= 0 && address + sizeof(T) <= bytes.size());
    std::memcpy(&value, bytes.data() + address, sizeof(T));
    return value;
  }

  template <typename T> const T &put(int address, const T &value) {
    assert(address >= 0 && address + sizeof(T) <= bytes.size());
    ++putCalls;
    const uint8_t *source = reinterpret_cast<const uint8_t *>(&value);
    for (size_t i = 0; i < sizeof(T); ++i) {
      if (bytes[address + i] != source[i]) {
        bytes[address + i] = source[i];
        ++changedBytes;
      }
    }
    return value;
  }

  // Populate the legacy float without counting a test fixture as a firmware write.
  void seed(float factor) { std::memcpy(bytes.data(), &factor, sizeof(factor)); }
};

using EEPROMClass = TestEEPROM;
extern TestEEPROM EEPROM;

#endif
