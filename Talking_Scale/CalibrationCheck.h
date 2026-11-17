#ifndef CALIBRATION_CHECK_H
#define CALIBRATION_CHECK_H

#include <math.h>
#include <stdint.h>

struct SampleSummary {
  int32_t mean;
  uint32_t spread;   // Largest filtered peak-to-peak range in the two windows.
  uint32_t movement; // Difference between the two independent window means.

  SampleSummary(int32_t value = 0, uint32_t range = 0, uint32_t change = 0)
      : mean(value), spread(range), movement(change) {}
};

// Raw sample noise is distinct from movement of an averaged reading. These
// defaults check mean repeatability without requiring every ADC conversion to
// fit inside the displayed hundredth of a gram.
const float CALIBRATION_STABILITY_GRAMS = 0.01f;
const uint32_t CALIBRATION_MIN_STABLE_COUNTS = 2;
const uint32_t CALIBRATION_MIN_SIGNAL_COUNTS = 32;
const uint32_t CALIBRATION_NOISE_MULTIPLIER = 10;

enum class CalibrationDecision : uint8_t {
  Valid, NoSignal, BaselineMoving, WeightMoving, InvalidFactor
};

inline bool validCalibrationFactor(float factor) {
  return isfinite(factor) && factor != 0.0f;
}

inline bool baselineReady(const SampleSummary &baseline) {
  // Establish zero without depending on the old calibration being correct.
  // Random noise can have a broad range while independent averages agree;
  // sustained movement through a window has a larger mean-to-mean change.
  uint32_t maximumMovement = baseline.spread / 2;
  if (maximumMovement < CALIBRATION_MIN_STABLE_COUNTS) {
    maximumMovement = CALIBRATION_MIN_STABLE_COUNTS;
  }
  return baseline.movement <= maximumMovement;
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

inline CalibrationDecision evaluateCalibration(const SampleSummary &baseline,
                                               const SampleSummary &loaded,
                                               float knownWeightGrams,
                                               float &factor) {
  if (!isfinite(knownWeightGrams) || knownWeightGrams <= 0.0f) {
    return CalibrationDecision::InvalidFactor;
  }
  if (!calibrationSignalValid(baseline, loaded)) {
    return CalibrationDecision::NoSignal;
  }

  const int64_t difference = static_cast<int64_t>(loaded.mean) - baseline.mean;
  const float candidate = static_cast<float>(difference) / knownWeightGrams;
  if (!validCalibrationFactor(candidate)) return CalibrationDecision::InvalidFactor;

  float maximumMovement = fabsf(candidate) * CALIBRATION_STABILITY_GRAMS;
  if (maximumMovement < CALIBRATION_MIN_STABLE_COUNTS) {
    maximumMovement = CALIBRATION_MIN_STABLE_COUNTS;
  }
  if (baseline.movement > maximumMovement) return CalibrationDecision::BaselineMoving;
  if (loaded.movement > maximumMovement) return CalibrationDecision::WeightMoving;

  // Failed attempts never change a previously usable calibration factor.
  factor = candidate;
  return CalibrationDecision::Valid;
}

inline bool calibrationCandidate(const SampleSummary &baseline,
                                 const SampleSummary &loaded,
                                 float knownWeightGrams, float &factor) {
  return evaluateCalibration(baseline, loaded, knownWeightGrams, factor) ==
         CalibrationDecision::Valid;
}

#endif
