#include "../Talking_Scale/ScaleFilter.h"

#include <assert.h>
#include <stdio.h>

void fill(ScaleFilter &filter, int32_t value, uint32_t start = 0) {
  for (uint32_t i = 0; i < 22; ++i) filter.add(value, start + i * 100);
}

void testPrimingAndReset() {
  ScaleFilter filter;
  assert(!filter.ready() && filter.count() == 0);
  assert(filter.mean() == 0 && filter.spread() == 0 && filter.meanSpread() == 0);
  assert(filter.latestRaw() == 0 && filter.lastSampleAt() == 0);
  assert(!filter.stable(1000.0f, true));
  filter.add(1000, 10);
  filter.add(1000, 110);
  assert(filter.count() == 0);
  for (uint32_t i = 0; i < 19; ++i) filter.add(1000, 210 + i * 100);
  assert(!filter.ready() && filter.count() == 19);
  filter.add(1000, 2110);
  assert(filter.ready() && filter.count() == 20);
  assert(filter.mean() == 1000 && filter.spread() == 0);
  assert(filter.latestRaw() == 1000 && filter.lastSampleAt() == 2110);
  assert(filter.stable(1000.0f, true));
  filter.reset();
  assert(!filter.ready() && filter.count() == 0);
  assert(filter.mean() == 0 && filter.spread() == 0 && filter.meanSpread() == 0);
  assert(filter.latestRaw() == 0 && filter.lastSampleAt() == 0);
  fill(filter, -500);
  assert(filter.mean() == -500 && filter.spread() == 0);
}

void testIsolatedSpikes() {
  ScaleFilter filter;
  // Even an initial spike is discarded rather than priming the average with it.
  filter.add(8388607, 0);
  for (uint32_t i = 1; i < 22; ++i) filter.add(0, i * 100);
  assert(filter.ready() && filter.mean() == 0 && filter.spread() == 0);
  filter.add(-8388608, 2200);
  filter.add(0, 2300);
  filter.add(0, 2400);
  assert(filter.mean() == 0 && filter.spread() == 0 && filter.meanSpread() == 0);
  filter.add(8388607, 2500);
  filter.add(0, 2600);
  filter.add(0, 2700);
  assert(filter.mean() == 0 && filter.spread() == 0 && filter.meanSpread() == 0);
}

void testStepAndContinuousUpdates() {
  ScaleFilter filter;
  fill(filter, 0);
  for (uint32_t i = 1; i <= 21; ++i) {
    filter.add(1000, 2100 + i * 100);
    assert(filter.ready() && filter.count() == 20);
    // The median introduces one conversion of delay, then each new conversion
    // replaces one old value instead of waiting for another twenty-value batch.
    assert(filter.mean() == static_cast<int32_t>((i - 1) * 50));
    if (i > 1 && i < 21) assert(!filter.stable(1000.0f, true));
  }
  assert(filter.mean() == 1000 && filter.spread() == 0);
  assert(filter.stable(-1000.0f, true));

  ScaleFilter ramp;
  for (uint32_t i = 0; i < 22; ++i) ramp.add(i, i * 100);
  assert(ramp.mean() == 10 && ramp.spread() == 19);
  ramp.add(22, 2200);
  assert(ramp.mean() == 11 && ramp.spread() == 19);
}

void testStabilityAndSmallChanges() {
  ScaleFilter filter;
  for (uint32_t i = 0; i < 22; ++i) filter.add(i, i * 100);
  assert(filter.meanSpread() == 15);
  assert(!filter.stable(1000.0f, true)); // Block-mean movement exceeds 0.01 g.
  assert(filter.stable(2000.0f, true));
  assert(filter.stable(-2000.0f, true));
  assert(filter.stable(NAN, false)); // Uncalibrated threshold uses raw counts.
  assert(!filter.stable(0.0f, true));
  assert(!filter.stable(NAN, true));
  assert(!filter.stable(INFINITY, true));

  ScaleFilter floor;
  for (uint32_t i = 0; i < 22; ++i) floor.add(i % 2 ? 3 : 1, i * 100);
  assert(floor.spread() == 2 && floor.stable(1.0f, true));
  floor.reset();
  for (uint32_t i = 0; i < 22; ++i) floor.add(i % 2 ? 4 : 1, i * 100);
  assert(floor.spread() == 3 && floor.meanSpread() <= 2);
  assert(floor.stable(1.0f, true)); // Balanced quantization noise is not movement.
  floor.reset();
  for (uint32_t i = 0; i < 22; ++i) floor.add(i, i * 100);
  assert(floor.meanSpread() == 15 && !floor.stable(1.0f, true));

  // The filter smooths a genuine hundredth-gram step; it never subtracts it or
  // imposes an announcement deadband that could conceal the settled change.
  filter.reset();
  fill(filter, 1000);
  fill(filter, 1010, 2200);
  assert(filter.mean() == 1010 && filter.stable(1000.0f, true));
}

void testRepeatableJitter() {
  ScaleFilter filter;
  for (uint32_t i = 0; i < 100; ++i) {
    filter.add(1000 + ((i % 2) ? 20 : -20), i * 100);
    if (filter.ready()) {
      assert(filter.mean() == 1000);
      assert(filter.spread() == 40);
      assert(filter.meanSpread() == 8);
      assert(filter.stable(2000.0f, true));
      assert(filter.stable(-2000.0f, true));
    }
  }
}

void testChronologicalBlockMeans() {
  ScaleFilter filter;
  for (uint32_t i = 0; i < 22; ++i) filter.add(i, i * 100);
  assert(filter.meanSpread() == 15);
  for (uint32_t i = 22; i < 70; ++i) {
    filter.add(i, i * 100);
    // The oldest-to-newest grouping stays correct at every ring offset.
    assert(filter.meanSpread() == 15);
    assert(!filter.stable(1000.0f, true));
  }
}

void testFullScaleInputsAndTimestampRollover() {
  ScaleFilter filter;
  fill(filter, 8388607);
  assert(filter.mean() == 8388607 && filter.meanSpread() == 0);
  filter.add(-8388608, 2200);
  filter.add(-8388608, 2300);
  assert(filter.meanSpread() == 3355443);
  assert(!filter.stable(1000.0f, true));
  fill(filter, -8388608);
  assert(filter.mean() == -8388608);
  filter.reset();
  const uint32_t start = UINT32_MAX - 150;
  fill(filter, -100, start);
  assert(filter.lastSampleAt() == static_cast<uint32_t>(start + 2100));
  assert(filter.latestRaw() == -100 && filter.mean() == -100);
}

int main() {
  testPrimingAndReset();
  testIsolatedSpikes();
  testStepAndContinuousUpdates();
  testStabilityAndSmallChanges();
  testRepeatableJitter();
  testChronologicalBlockMeans();
  testFullScaleInputsAndTimestampRollover();
  puts("Filter tests passed.");
}
