#include "../Talking_Scale/CalibrationCheck.h"
#include "../Talking_Scale/CalibrationSampler.h"

#include <assert.h>
#include <stdio.h>

void testStoredFactors() {
  assert(validCalibrationFactor(1000.0f));
  assert(validCalibrationFactor(-1000.0f));
  assert(!validCalibrationFactor(0.0f));
  assert(!validCalibrationFactor(-0.0f));
  assert(!validCalibrationFactor(NAN));
  assert(!validCalibrationFactor(INFINITY));
  assert(!validCalibrationFactor(-INFINITY));
}

void testBaselineReadiness() {
  assert(baselineReady({1500, 40, 0}));
  assert(baselineReady({1500, 40, 20}));
  assert(!baselineReady({1500, 40, 21}));
  assert(baselineReady({1500, 0, 2}));
  assert(!baselineReady({1500, 0, 3}));
  // The old calibration factor is deliberately not an input to this check.
  assert(baselineReady({1500, 400, 2}));
}

void testNoiseAndSignal() {
  const SampleSummary baseline(1000, 4);
  assert(!calibrationSignalValid(baseline, {1001, 4}));
  assert(!calibrationSignalValid(baseline, {1040, 4}));
  assert(calibrationSignalValid(baseline, {1041, 4}));
  assert(!calibrationSignalValid({1000, 0}, {1032, 0}));
  assert(calibrationSignalValid({1000, 0}, {1033, 0}));
  assert(!calibrationSignalValid({1000, 1}, {1300, 30}));
  assert(calibrationSignalValid({1000, 1}, {1301, 30}));
  assert(calibrationSignalValid(baseline, {-9000, 4}));
  // Invalid large spreads must not wrap the signal/noise comparison.
  assert(!calibrationSignalValid({INT32_MIN, UINT32_MAX}, {INT32_MAX, 0}));
  assert(!calibrationSignalValid({INT32_MAX, 0}, {INT32_MIN, UINT32_MAX}));
}

void testCandidates() {
  float factor = 1234.0f;
  assert(calibrationCandidate({1500, 40, 2}, {11500, 40, 3}, 5.0f, factor));
  assert(factor == 2000.0f);
  assert(calibrationCandidate({1500, 40, 2}, {-8500, 40, 3}, 5.0f, factor));
  assert(factor == -2000.0f);
  // Broad but repeatable raw noise no longer makes an accurate average fail.
  assert(calibrationCandidate({1500, 100, 2}, {11500, 100, 2}, 5.0f, factor));
  assert(factor == 2000.0f);

  factor = 1234.0f;
  assert(evaluateCalibration({1500, 4, 0}, {1501, 4, 0}, 5.0f, factor) ==
         CalibrationDecision::NoSignal);
  assert(evaluateCalibration({1500, 40, 21}, {11500, 40, 0}, 5.0f, factor) ==
         CalibrationDecision::BaselineMoving);
  assert(evaluateCalibration({1500, 40, 0}, {11500, 40, 21}, 5.0f, factor) ==
         CalibrationDecision::WeightMoving);
  assert(evaluateCalibration({1500, 0, 0}, {11500, 0, 0}, 0.0f, factor) ==
         CalibrationDecision::InvalidFactor);
  assert(!calibrationCandidate({1500, 0}, {11500, 0}, -5.0f, factor));
  assert(!calibrationCandidate({1500, 0}, {11500, 0}, NAN, factor));
  assert(!calibrationCandidate({1500, 0}, {11500, 0}, INFINITY, factor));
  assert(factor == 1234.0f);
}

SampleSummary alternatingSamples(int32_t centre, uint32_t &rawSpread) {
  CalibrationSampler sampler;
  for (uint8_t i = 0; i < 42; ++i) {
    const bool reported = sampler.add(centre + ((i % 2) ? 20 : -20));
    assert(reported == (i == 41));
  }
  rawSpread = sampler.rawSpread();
  return sampler.summary();
}

void testRealisticJitter() {
  uint32_t rawSpread;
  const SampleSummary baseline = alternatingSamples(1500, rawSpread);
  assert(rawSpread == 40 && baseline.spread == 40 && baseline.movement == 0);
  const SampleSummary loaded = alternatingSamples(11500, rawSpread);
  assert(baselineReady(baseline));
  float factor = 0.0f;
  assert(calibrationCandidate(baseline, loaded, 5.0f, factor));
  assert(factor == 2000.0f);

  const SampleSummary reversed = alternatingSamples(-8500, rawSpread);
  assert(calibrationCandidate(baseline, reversed, 5.0f, factor));
  assert(factor == -2000.0f);

  const SampleSummary noiseOnly = alternatingSamples(1501, rawSpread);
  factor = 1234.0f;
  assert(!calibrationCandidate(baseline, noiseOnly, 5.0f, factor));
  assert(factor == 1234.0f);
}

void testIsolatedSpikes() {
  CalibrationSampler sampler;
  unsigned int reports = 0;
  for (uint8_t i = 0; i < 82; ++i) {
    const bool ready = sampler.add(i == 11 ? 500000 : 1500);
    if (ready) {
      ++reports;
      assert(sampler.summary().mean == 1500);
      assert(sampler.summary().spread == 0);
      assert(sampler.summary().movement == 0);
      if (reports == 1) assert(sampler.rawSpread() == 498500);
    }
  }
  assert(reports == 3);
}

void testIndependentWindowsAndDrift() {
  CalibrationSampler sampler;
  for (uint8_t i = 0; i < 22; ++i) assert(!sampler.add(1000));
  assert(sampler.windowCount() == 1 && sampler.count() == 0);
  for (uint8_t i = 0; i < 19; ++i) assert(!sampler.add(1000));
  assert(sampler.count() == 19);
  assert(sampler.add(1000));
  assert(sampler.windowCount() == 2 && sampler.count() == 0);
  assert(sampler.summary().mean == 1000 && sampler.summary().movement == 0);
  for (uint8_t i = 0; i < 19; ++i) assert(!sampler.add(1000));
  assert(sampler.add(1000)); // A new report requires twenty new filtered samples.

  sampler.reset();
  assert(sampler.count() == 0 && sampler.windowCount() == 0);
  assert(sampler.rawSpread() == 0);
  for (int32_t value = 1000; value < 1042; ++value) sampler.add(value);
  assert(sampler.windowCount() == 2);
  assert(sampler.summary().movement == 20);
  assert(sampler.summary().spread == 19);
  assert(!baselineReady(sampler.summary()));

  const SampleSummary loaded = sampler.summary();
  float factor = 1234.0f;
  assert(evaluateCalibration({-3980, 1, 0}, loaded, 5.0f, factor) ==
         CalibrationDecision::WeightMoving);
  assert(factor == 1234.0f);
}

int main() {
  testStoredFactors();
  testBaselineReadiness();
  testNoiseAndSignal();
  testCandidates();
  testRealisticJitter();
  testIsolatedSpikes();
  testIndependentWindowsAndDrift();
  puts("Calibration validation and sampling tests passed.");
}
