#include "../Talking_Scale/CalibrationStore.h"

#include <array>
#include <assert.h>
#include <stdio.h>

struct PowerCut {};

struct MemoryEEPROM {
  std::array<uint8_t, 64> bytes;
  int writes = 0;
  int writesBeforeCut = -1;
  int droppedAddress = -1;
  int droppedValue = -1;
  MemoryEEPROM() { bytes.fill(0xff); }
  uint8_t read(int address) {
    assert(address >= 0 && address < static_cast<int>(bytes.size()));
    return bytes[address];
  }
  void update(int address, uint8_t value) {
    assert(address >= 0 && address < static_cast<int>(bytes.size()));
    if (bytes[address] == value) return;
    if (address == droppedAddress && (droppedValue < 0 || value == droppedValue)) return;
    if (writesBeforeCut == 0) throw PowerCut();
    if (writesBeforeCut > 0) --writesBeforeCut;
    bytes[address] = value;
    ++writes;
  }
  void seedLegacy(float factor) {
    uint32_t bits;
    memcpy(&bits, &factor, 4);
    for (uint8_t i = 0; i < 4; ++i) bytes[i] = static_cast<uint8_t>(bits >> (8 * i));
  }
};

// Helpers allow specific serialized records, including sequence wrap and a
// correctly checksummed unknown future format, to be tested independently.
uint16_t crc(const uint8_t *record) {
  uint16_t result = 0xffff;
  for (uint8_t i = 1; i < 14; ++i) {
    result ^= static_cast<uint16_t>(record[i]) << 8;
    for (uint8_t bit = 0; bit < 8; ++bit) {
      result = (result & 0x8000) ? static_cast<uint16_t>((result << 1) ^ 0x1021)
                                 : static_cast<uint16_t>(result << 1);
    }
  }
  return result;
}

void setSequence(MemoryEEPROM &memory, uint8_t slot, uint32_t sequence) {
  uint8_t *record = memory.bytes.data() + 16 + slot * 16;
  for (uint8_t i = 0; i < 4; ++i) record[2 + i] = static_cast<uint8_t>(sequence >> (8 * i));
  const uint16_t checksum = crc(record);
  record[14] = static_cast<uint8_t>(checksum);
  record[15] = static_cast<uint8_t>(checksum >> 8);
}

void expectFactor(MemoryEEPROM &memory, float expected) {
  CalibrationStore<MemoryEEPROM> reboot(memory);
  float factor = 0;
  assert(reboot.load(factor));
  assert(factor == expected);
}

void testEmptyAndInvalid() {
  MemoryEEPROM memory;
  CalibrationStore<MemoryEEPROM> store(memory);
  float factor = 42;
  assert(!store.load(factor) && factor == 42 && memory.writes == 0);
  assert(!store.save(0) && !store.save(NAN) && !store.save(INFINITY));
  assert(memory.writes == 0);
  memory.seedLegacy(0);
  assert(!store.load(factor) && memory.writes == 0);
  memory.seedLegacy(NAN);
  assert(!store.load(factor) && memory.writes == 0);
}

void testMigrationAndNoOp() {
  MemoryEEPROM memory;
  memory.seedLegacy(-1234.5f);
  const auto original = memory.bytes;
  CalibrationStore<MemoryEEPROM> store(memory);
  float factor;
  assert(store.load(factor) && factor == -1234.5f);
  assert(store.source() == CalibrationSource::Legacy);
  assert(memory.bytes[8] == 0xa5);
  for (uint8_t i = 0; i < 8; ++i) assert(memory.bytes[i] == original[i]);
  const int writes = memory.writes;
  assert(store.save(factor));
  assert(memory.writes == writes);
  expectFactor(memory, factor);
  assert(memory.writes == writes);
  assert(store.save(2000));
  expectFactor(memory, 2000);
  for (uint8_t i = 0; i < 8; ++i) assert(memory.bytes[i] == original[i]);
}

void testCorruptionAndVersion() {
  MemoryEEPROM original;
  original.seedLegacy(1000);
  CalibrationStore<MemoryEEPROM> store(original);
  float factor;
  assert(store.load(factor));
  assert(store.save(2000));
  // Every single-byte corruption in the newest record must select its backup.
  for (int i = 32; i < 48; ++i) {
    MemoryEEPROM damaged = original;
    damaged.bytes[i] ^= 0x01;
    expectFactor(damaged, 1000);
  }
  MemoryEEPROM future = original;
  future.bytes[33] = 2;
  const uint16_t checksum = crc(future.bytes.data() + 32);
  future.bytes[46] = static_cast<uint8_t>(checksum);
  future.bytes[47] = static_cast<uint8_t>(checksum >> 8);
  expectFactor(future, 1000);
  original.bytes[16] = 0;
  original.bytes[32] = 0;
  CalibrationStore<MemoryEEPROM> damaged(original);
  factor = 42;
  const int writes = original.writes;
  assert(!damaged.load(factor) && factor == 42);
  assert(original.writes == writes); // Never resurrect the legacy value.
  assert(damaged.save(3000)); // Explicit calibration can recover damaged storage.
  expectFactor(original, 3000);
}

void testSequenceWrap() {
  MemoryEEPROM memory;
  CalibrationStore<MemoryEEPROM> store(memory);
  assert(store.save(1000));
  assert(store.save(2000));
  setSequence(memory, 0, UINT32_MAX);
  setSequence(memory, 1, 0);
  expectFactor(memory, 2000);
  assert(store.save(3000));
  expectFactor(memory, 3000);
  assert(memory.bytes[18] == 1); // Wrapped sequence 1 in slot zero.
}

void testMigrationPowerCuts() {
  MemoryEEPROM original;
  original.seedLegacy(1000);
  MemoryEEPROM completed = original;
  CalibrationStore<MemoryEEPROM> complete(completed);
  float factor;
  assert(complete.load(factor));
  const int totalWrites = completed.writes;
  for (int cut = 0; cut <= totalWrites; ++cut) {
    MemoryEEPROM memory = original;
    memory.writesBeforeCut = cut;
    CalibrationStore<MemoryEEPROM> interrupted(memory);
    try { interrupted.load(factor); } catch (const PowerCut &) {}
    memory.writesBeforeCut = -1;
    expectFactor(memory, 1000);
    for (uint8_t i = 0; i < 8; ++i) assert(memory.bytes[i] == original.bytes[i]);
  }
}

void testSavePowerCuts(bool migrateFirst) {
  MemoryEEPROM original;
  original.seedLegacy(1000);
  if (migrateFirst) {
    CalibrationStore<MemoryEEPROM> initialize(original);
    float factor;
    assert(initialize.load(factor));
    assert(initialize.save(1500)); // Exercise overwriting a committed old slot.
  }
  const float previous = migrateFirst ? 1500.0f : 1000.0f;
  MemoryEEPROM completed = original;
  CalibrationStore<MemoryEEPROM> complete(completed);
  assert(complete.save(-2345));
  const int totalWrites = completed.writes - original.writes;
  for (int cut = 0; cut <= totalWrites; ++cut) {
    MemoryEEPROM memory = original;
    memory.writesBeforeCut = cut;
    CalibrationStore<MemoryEEPROM> interrupted(memory);
    try { interrupted.save(-2345); } catch (const PowerCut &) {}
    memory.writesBeforeCut = -1;
    CalibrationStore<MemoryEEPROM> reboot(memory);
    float recovered;
    assert(reboot.load(recovered));
    assert(recovered == previous || recovered == -2345.0f);
    for (uint8_t i = 0; i < 8; ++i) assert(memory.bytes[i] == original.bytes[i]);
  }
}

void testFirstCalibrationPowerCuts() {
  MemoryEEPROM completed;
  CalibrationStore<MemoryEEPROM> complete(completed);
  assert(complete.save(1234.5f));
  for (int cut = 0; cut <= completed.writes; ++cut) {
    MemoryEEPROM memory;
    memory.writesBeforeCut = cut;
    CalibrationStore<MemoryEEPROM> interrupted(memory);
    try { interrupted.save(1234.5f); } catch (const PowerCut &) {}
    memory.writesBeforeCut = -1;
    CalibrationStore<MemoryEEPROM> reboot(memory);
    float recovered = 42;
    if (reboot.load(recovered)) assert(recovered == 1234.5f);
    else assert(recovered == 42); // No prior calibration exists to recover.
  }
}

void testDroppedMigrationWrites() {
  // Dropping any payload byte, invalidation, commit, or seal must not publish a
  // migration as successful. The untouched legacy factor remains recoverable.
  for (int address = 16; address < 32; ++address) {
    MemoryEEPROM memory;
    memory.seedLegacy(1000);
    memory.droppedAddress = address;
    CalibrationStore<MemoryEEPROM> store(memory);
    float factor = 42;
    assert(!store.load(factor));
    assert(factor == 42 && store.source() == CalibrationSource::None);
    assert(memory.bytes[8] == 0xff); // Failed payload verification never seals.
    memory.droppedAddress = -1;
    expectFactor(memory, 1000);
  }
  MemoryEEPROM commit;
  commit.seedLegacy(1000);
  commit.droppedAddress = 16;
  commit.droppedValue = 0xa5;
  CalibrationStore<MemoryEEPROM> store(commit);
  float factor = 42;
  assert(!store.load(factor) && factor == 42 && commit.bytes[8] == 0xff);
  commit.droppedAddress = -1;
  expectFactor(commit, 1000);
}

void testDroppedSaveWrites() {
  MemoryEEPROM original;
  original.seedLegacy(1000);
  CalibrationStore<MemoryEEPROM> initialize(original);
  float factor;
  assert(initialize.load(factor));
  // These addresses all differ between an erased slot and the candidate record.
  for (int address = 32; address < 48; ++address) {
    MemoryEEPROM memory = original;
    memory.droppedAddress = address;
    CalibrationStore<MemoryEEPROM> store(memory);
    assert(!store.save(2000));
    expectFactor(memory, 1000);
  }
  MemoryEEPROM commit = original;
  commit.droppedAddress = 32;
  commit.droppedValue = 0xa5;
  CalibrationStore<MemoryEEPROM> store(commit);
  assert(!store.save(2000));
  expectFactor(commit, 1000);

  // A target stuck at its old committed marker must be detected before its
  // payload is changed, retaining both the current factor and old backup.
  assert(initialize.save(1500));
  const auto previousBytes = original.bytes;
  original.droppedAddress = 16;
  original.droppedValue = 0;
  assert(!initialize.save(2000));
  assert(original.bytes == previousBytes);
  expectFactor(original, 1500);
}

void testDroppedSeal() {
  MemoryEEPROM memory;
  memory.seedLegacy(1000);
  memory.droppedAddress = 8;
  CalibrationStore<MemoryEEPROM> store(memory);
  float factor = 42;
  assert(!store.load(factor) && factor == 42); // Migration has not been sealed.
  assert(memory.bytes[8] == 0xff);
  assert(store.load(factor) && factor == 1000); // Verified journal still loads.
  assert(!store.save(2000)); // A new factor cannot be saved with a broken seal.
  expectFactor(memory, 1000);

  MemoryEEPROM empty;
  empty.droppedAddress = 8;
  CalibrationStore<MemoryEEPROM> first(empty);
  assert(!first.save(2000));
  factor = 42;
  assert(!first.load(factor) && factor == 42);
  for (int address = 16; address < 48; ++address) assert(empty.bytes[address] == 0xff);
}

int main() {
  testEmptyAndInvalid();
  testMigrationAndNoOp();
  testCorruptionAndVersion();
  testSequenceWrap();
  testMigrationPowerCuts();
  testSavePowerCuts(false);
  testSavePowerCuts(true);
  testFirstCalibrationPowerCuts();
  testDroppedMigrationWrites();
  testDroppedSaveWrites();
  testDroppedSeal();
  puts("Calibration storage tests passed.");
}
