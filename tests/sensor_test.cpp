#include "../Talking_Scale/SensorHealth.h"

#include <assert.h>
#include <stdio.h>

void testOrdinaryReadings() {
  SensorHealth sensor;
  assert(!sensor.faulted() && !sensor.clipped());
  for (int i = 0; i < 1000; ++i) {
    assert(sensor.add(0) == SensorSample::Accept);
    assert(sensor.add(-1) == SensorSample::Accept);
    assert(sensor.add(1234) == SensorSample::Accept);
  }
  assert(sensor.add(8388606) == SensorSample::Accept);
  assert(sensor.add(-8388607) == SensorSample::Accept);
  assert(!sensor.faulted());
}

void testIsolatedClipping() {
  SensorHealth sensor;
  assert(sensor.add(8388607) == SensorSample::Reject);
  assert(!sensor.faulted() && !sensor.clipped());
  assert(sensor.add(0) == SensorSample::Accept);
  assert(sensor.add(-8388608) == SensorSample::Reject);
  assert(sensor.add(-8388608) == SensorSample::Reject);
  assert(!sensor.faulted());
  assert(sensor.add(-1) == SensorSample::Accept);
  assert(sensor.add(8388607) == SensorSample::Reject);
  assert(!sensor.faulted());
}

void assertClippingFault(int32_t first, int32_t second, int32_t third) {
  SensorHealth sensor;
  assert(sensor.add(first) == SensorSample::Reject);
  assert(sensor.add(second) == SensorSample::Reject);
  assert(sensor.add(third) == SensorSample::Fault);
  assert(sensor.faulted() && sensor.clipped());
  // Persistent rail readings cannot overflow a counter or repeatedly report Fault.
  for (int i = 0; i < 1000; ++i) {
    assert(sensor.add(first) == SensorSample::Reject);
  }
  assert(sensor.faulted() && sensor.clipped());
}

void testClippingFaults() {
  assertClippingFault(8388607, 8388607, 8388607);
  assertClippingFault(-8388608, -8388608, -8388608);
  assertClippingFault(8388607, -8388608, 8388607);
  assertClippingFault(INT32_MAX, INT32_MIN, INT32_MAX);
}

void testRecovery() {
  SensorHealth sensor;
  sensor.add(8388607);
  sensor.add(8388607);
  assert(sensor.add(8388607) == SensorSample::Fault);
  for (int i = 0; i < 4; ++i) {
    assert(sensor.add(1000) == SensorSample::Reject);
    assert(sensor.faulted() && sensor.clipped());
  }
  assert(sensor.add(1000) == SensorSample::Recovered);
  assert(!sensor.faulted() && !sensor.clipped());
  assert(sensor.add(1000) == SensorSample::Accept);
  assert(sensor.add(8388607) == SensorSample::Reject);
  assert(sensor.add(8388607) == SensorSample::Reject);
  assert(sensor.add(8388607) == SensorSample::Fault);
}

void testInterruptedRecovery() {
  SensorHealth sensor;
  sensor.timeout();
  for (int i = 0; i < 4; ++i) assert(sensor.add(0) == SensorSample::Reject);
  assert(sensor.add(8388607) == SensorSample::Reject);
  for (int i = 0; i < 4; ++i) assert(sensor.add(-1) == SensorSample::Reject);
  assert(sensor.faulted() && !sensor.clipped());
  assert(sensor.add(-1) == SensorSample::Recovered);
  assert(!sensor.faulted());
}

void testTimeoutAndCauseChange() {
  SensorHealth sensor;
  sensor.timeout();
  assert(sensor.faulted() && !sensor.clipped());
  assert(sensor.add(8388607) == SensorSample::Reject);
  assert(sensor.add(-8388608) == SensorSample::Reject);
  assert(sensor.add(8388607) == SensorSample::Fault);
  assert(sensor.faulted() && sensor.clipped());
  sensor.timeout();
  assert(sensor.faulted() && !sensor.clipped());
  for (int i = 0; i < 4; ++i) assert(sensor.add(1234) == SensorSample::Reject);
  // A second timeout invalidates partial recovery.
  sensor.timeout();
  for (int i = 0; i < 4; ++i) assert(sensor.add(1234) == SensorSample::Reject);
  assert(sensor.add(1234) == SensorSample::Recovered);
  assert(!sensor.faulted() && !sensor.clipped());
}

int main() {
  testOrdinaryReadings();
  testIsolatedClipping();
  testClippingFaults();
  testRecovery();
  testInterruptedRecovery();
  testTimeoutAndCauseChange();
  puts("Sensor health tests passed.");
}
