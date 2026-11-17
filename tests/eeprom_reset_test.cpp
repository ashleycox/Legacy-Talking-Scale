#include "../Talking_Scale/EEPROMReset.h"

#include <assert.h>
#include <stdio.h>
#include <vector>

struct PowerCut {};

struct ResetStorage {
  std::vector<uint8_t> bytes;
  int writes = 0;
  int updates = 0;
  int writesBeforeCut = -1;
  int droppedAddress = -1;
  int droppedValue = -1;

  explicit ResetStorage(int capacity) : bytes(capacity, 0xff) {}
  int length() const { return static_cast<int>(bytes.size()); }
  uint8_t read(int address) const {
    assert(address >= 0 && address < length());
    return bytes[address];
  }
  void update(int address, uint8_t value) {
    assert(address >= 0 && address < length());
    ++updates;
    if (bytes[address] == value) return;
    if (address == droppedAddress && (droppedValue < 0 || value == droppedValue)) return;
    if (writesBeforeCut == 0) throw PowerCut();
    if (writesBeforeCut > 0) --writesBeforeCut;
    bytes[address] = value;
    ++writes;
  }
  void fill() {
    for (int address = 0; address < length(); ++address) {
      bytes[address] = static_cast<uint8_t>(address % 254);
    }
    if (length() > 9) bytes[9] = 0xff;
  }
};

void expectErased(const ResetStorage &storage) {
  for (uint8_t value : storage.bytes) assert(value == 0xff);
}

EEPROMResetResult finish(EEPROMReset<ResetStorage> &reset, ResetStorage &storage) {
  for (int count = 0; count <= storage.length(); ++count) {
    const int before = storage.updates;
    const EEPROMResetResult result = reset.step();
    assert(storage.updates - before <= 1);
    if (result != EEPROMResetResult::Busy) return result;
    assert(reset.pending() && storage.bytes[9] == 0x52);
  }
  assert(false); // A full wipe takes at most length() calls, including its marker.
  return EEPROMResetResult::Failed;
}

void testFullWipe(int capacity) {
  ResetStorage storage(capacity);
  storage.fill();
  EEPROMReset<ResetStorage> reset(storage);
  assert(!reset.pending());
  assert(reset.begin() && reset.pending());
  assert(storage.bytes[9] == 0x52 && storage.bytes[0] == 0);
  assert(finish(reset, storage) == EEPROMResetResult::Complete);
  assert(!reset.pending());
  expectErased(storage);
  const int writes = storage.writes;
  assert(reset.step() == EEPROMResetResult::Complete && storage.writes == writes);
}

void testAlreadyErased() {
  ResetStorage storage(1024);
  EEPROMReset<ResetStorage> reset(storage);
  assert(reset.begin());
  assert(finish(reset, storage) == EEPROMResetResult::Complete);
  assert(storage.updates == 2 && storage.writes == 2); // Only set/clear the marker.
  expectErased(storage);
}

void testMarkerFailure() {
  ResetStorage storage(64);
  storage.fill();
  const auto original = storage.bytes;
  storage.droppedAddress = 9;
  EEPROMReset<ResetStorage> reset(storage);
  assert(!reset.begin());
  assert(reset.step() == EEPROMResetResult::Failed);
  assert(!reset.pending() && storage.bytes == original);
  storage.droppedAddress = -1;
  assert(reset.begin());
  assert(finish(reset, storage) == EEPROMResetResult::Complete);
  expectErased(storage);
}

void testDataFailure(int address) {
  ResetStorage storage(64);
  storage.fill();
  EEPROMReset<ResetStorage> reset(storage);
  assert(reset.begin());
  storage.droppedAddress = address;
  assert(finish(reset, storage) == EEPROMResetResult::Failed);
  assert(reset.pending());
  const auto failedBytes = storage.bytes;
  assert(reset.step() == EEPROMResetResult::Failed && storage.bytes == failedBytes);
  storage.droppedAddress = -1;
  EEPROMReset<ResetStorage> reboot(storage);
  assert(reboot.pending());
  // A new object resumes without needing begin() or another button hold.
  assert(finish(reboot, storage) == EEPROMResetResult::Complete);
  expectErased(storage);
}

void testFinalMarkerFailure() {
  ResetStorage storage(64);
  storage.fill();
  EEPROMReset<ResetStorage> reset(storage);
  assert(reset.begin());
  storage.droppedAddress = 9;
  storage.droppedValue = 0xff;
  assert(finish(reset, storage) == EEPROMResetResult::Failed);
  for (int address = 0; address < storage.length(); ++address) {
    assert(storage.bytes[address] == (address == 9 ? 0x52 : 0xff));
  }
  assert(reset.pending());
  storage.droppedAddress = -1;
  EEPROMReset<ResetStorage> reboot(storage);
  assert(finish(reboot, storage) == EEPROMResetResult::Complete);
  expectErased(storage);
}

void testPowerCuts(int capacity) {
  ResetStorage original(capacity);
  original.fill();
  ResetStorage completed = original;
  EEPROMReset<ResetStorage> complete(completed);
  assert(complete.begin());
  assert(finish(complete, completed) == EEPROMResetResult::Complete);
  for (int cut = 0; cut <= completed.writes; ++cut) {
    ResetStorage interrupted = original;
    interrupted.writesBeforeCut = cut;
    EEPROMReset<ResetStorage> reset(interrupted);
    try {
      assert(reset.begin());
      finish(reset, interrupted);
    } catch (const PowerCut &) {}
    interrupted.writesBeforeCut = -1;
    EEPROMReset<ResetStorage> reboot(interrupted);
    if (reboot.pending()) {
      assert(finish(reboot, interrupted) == EEPROMResetResult::Complete);
      expectErased(interrupted);
    } else if (cut == 0) {
      // Loss of power before the marker write leaves the original EEPROM intact.
      assert(interrupted.bytes == original.bytes);
    } else {
      expectErased(interrupted);
    }
  }
}

void testSmallStorageAndIdle() {
  for (int capacity = 0; capacity <= 9; ++capacity) {
    ResetStorage storage(capacity);
    EEPROMReset<ResetStorage> reset(storage);
    assert(!reset.pending() && !reset.begin());
    assert(reset.step() == EEPROMResetResult::Failed);
    assert(storage.updates == 0);
  }
  ResetStorage storage(64);
  EEPROMReset<ResetStorage> idle(storage);
  assert(idle.step() == EEPROMResetResult::Complete && storage.updates == 0);
}

int main() {
  testFullWipe(64);
  testFullWipe(1024);
  testAlreadyErased();
  testMarkerFailure();
  testDataFailure(0);
  testDataFailure(63);
  testFinalMarkerFailure();
  testPowerCuts(64);
  testPowerCuts(1024);
  testSmallStorageAndIdle();
  puts("EEPROM reset tests passed.");
}
