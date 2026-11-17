#include "../Talking_Scale/CalibrationCheck.h"

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

void testStability() {
  assert(stableCalibrationWindow({1000, 64}, 0.0f, false));
  assert(!stableCalibrationWindow({1000, 65}, 0.0f, false));
  assert(stableCalibrationWindow({1000, 10}, 1000.0f, true));
  assert(stableCalibrationWindow({1000, 10}, -1000.0f, true));
  assert(!stableCalibrationWindow({1000, 11}, 1000.0f, true));
  assert(stableCalibrationWindow({1000, 2}, 10.0f, true));
  assert(!stableCalibrationWindow({1000, 3}, 10.0f, true));
  assert(!stableCalibrationWindow({1000, 0}, 0.0f, true));
  assert(!stableCalibrationWindow({1000, 0}, NAN, true));
}

void testNoiseAndSignal() {
  const SampleSummary baseline = {1000, 4};
  assert(!calibrationSignalValid(baseline, {1001, 4}));
  assert(!calibrationSignalValid(baseline, {1040, 4}));
  assert(calibrationSignalValid(baseline, {1041, 4}));
  assert(!calibrationSignalValid({1000, 0}, {1032, 0}));
  assert(calibrationSignalValid({1000, 0}, {1033, 0}));
  assert(!calibrationSignalValid({1000, 1}, {1300, 30}));
  assert(calibrationSignalValid({1000, 1}, {1301, 30}));
  assert(calibrationSignalValid(baseline, {-9000, 4}));
  // Invalid large spreads cannot wrap their noise thresholds into a valid signal.
  assert(!calibrationSignalValid({INT32_MIN, UINT32_MAX}, {INT32_MAX, 0}));
  assert(!calibrationSignalValid({INT32_MAX, 0}, {INT32_MIN, UINT32_MAX}));
}

void testCandidates() {
  float factor = 1234.0f;
  assert(calibrationCandidate({1500, 10}, {11500, 15}, 5.0f, factor));
  assert(factor == 2000.0f);
  assert(calibrationCandidate({1500, 10}, {-8500, 15}, 5.0f, factor));
  assert(factor == -2000.0f);

  factor = 1234.0f;
  assert(!calibrationCandidate({1500, 4}, {1501, 4}, 5.0f, factor));
  assert(factor == 1234.0f);
  // A large signal alone does not establish stable loaded or empty measurements.
  assert(!calibrationCandidate({1500, 10}, {11500, 21}, 5.0f, factor));
  assert(!calibrationCandidate({1500, 21}, {11500, 10}, 5.0f, factor));
  // The initial raw-count baseline check is provisional until scale is known.
  assert(stableCalibrationWindow({1000, 64}, 0.0f, false));
  assert(!calibrationCandidate({1000, 64}, {2000, 1}, 5.0f, factor));
  assert(!calibrationCandidate({1500, 0}, {11500, 0}, 0.0f, factor));
  assert(!calibrationCandidate({1500, 0}, {11500, 0}, -5.0f, factor));
  assert(!calibrationCandidate({1500, 0}, {11500, 0}, NAN, factor));
  assert(!calibrationCandidate({1500, 0}, {11500, 0}, INFINITY, factor));
  assert(factor == 1234.0f);
}

int main() {
  testStoredFactors();
  testStability();
  testNoiseAndSignal();
  testCandidates();
  puts("Calibration validation tests passed.");
}
