#ifndef CALIBRATION_CHECK_H
#define CALIBRATION_CHECK_H

#include <math.h>
#include <stdint.h>

struct SampleSummary {
  int32_t mean;
  uint32_t spread; // Maximum minus minimum raw sample in the window.
};

// These conservative defaults need checking against the actual load cell's noise.
// The gram limit is the full peak-to-peak spread, not a change-detection deadband.
const float CALIBRATION_STABILITY_GRAMS = 0.01f;
const uint32_t CALIBRATION_MIN_STABLE_COUNTS = 2;
const uint32_t CALIBRATION_UNCALIBRATED_SPREAD = 64;
const uint32_t CALIBRATION_MIN_SIGNAL_COUNTS = 32;
const uint32_t CALIBRATION_NOISE_MULTIPLIER = 10;

inline bool validCalibrationFactor(float factor) {
  return isfinite(factor) && factor != 0.0f;
}

inline bool stableCalibrationWindow(const SampleSummary &samples,
                                    float referenceFactor, bool calibrated) {
  if (!calibrated) {
    return samples.spread <= CALIBRATION_UNCALIBRATED_SPREAD;
  }
  if (!validCalibrationFactor(referenceFactor)) return false;

  float limit = fabsf(referenceFactor) * CALIBRATION_STABILITY_GRAMS;
  if (limit < CALIBRATION_MIN_STABLE_COUNTS) {
    limit = CALIBRATION_MIN_STABLE_COUNTS;
  }
  return samples.spread <= limit;
}

inline bool calibrationSignalValid(const SampleSummary &baseline,
                                   const SampleSummary &loaded) {
  const int64_t difference = static_cast<int64_t>(loaded.mean) - baseline.mean;
  const uint64_t signal = difference < 0 ? static_cast<uint64_t>(-difference)
                                          : static_cast<uint64_t>(difference);
  uint64_t required = CALIBRATION_MIN_SIGNAL_COUNTS;
  const uint64_t baselineNoise = static_cast<uint64_t>(baseline.spread) *
                                 CALIBRATION_NOISE_MULTIPLIER;
  const uint64_t loadedNoise = static_cast<uint64_t>(loaded.spread) *
                               CALIBRATION_NOISE_MULTIPLIER;
  if (baselineNoise > required) required = baselineNoise;
  if (loadedNoise > required) required = loadedNoise;
  return signal > required;
}

inline bool calibrationCandidate(const SampleSummary &baseline,
                                 const SampleSummary &loaded,
                                 float knownWeightGrams, float &factor) {
  if (!isfinite(knownWeightGrams) || knownWeightGrams <= 0.0f ||
      !calibrationSignalValid(baseline, loaded)) return false;

  const int64_t difference = static_cast<int64_t>(loaded.mean) - baseline.mean;
  const float candidate = static_cast<float>(difference) / knownWeightGrams;
  if (!validCalibrationFactor(candidate) ||
      !stableCalibrationWindow(baseline, candidate, true) ||
      !stableCalibrationWindow(loaded, candidate, true)) return false;

  // Publish only a complete, validated result; failed attempts preserve the old value.
  factor = candidate;
  return true;
}

#endif
