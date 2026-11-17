#ifndef CALIBRATION_SAMPLER_H
#define CALIBRATION_SAMPLER_H

#include "CalibrationCheck.h"

// Independent windows of twenty median-filtered samples establish repeatability
// of the averaged reading. Windows do not overlap; reports compare the newest
// completed window with the preceding one.
class CalibrationSampler {
public:
  static const uint8_t WINDOW_SAMPLES = 20;

  void reset() {
    rawCount = nextRaw = sampleCount = completedWindows = 0;
    sum = 0;
    rawWindowStarted = false;
    previous = SampleSummary();
    result = SampleSummary();
    previousRawSpread = pairRawSpread = 0;
  }

  // Returns true only when a new window completes and a prior window exists.
  bool add(int32_t raw) {
    if (!rawWindowStarted) {
      rawMinimum = rawMaximum = raw;
      rawWindowStarted = true;
    } else {
      if (raw < rawMinimum) rawMinimum = raw;
      if (raw > rawMaximum) rawMaximum = raw;
    }
    rawValues[nextRaw] = raw;
    nextRaw = (nextRaw + 1) % 3;
    if (rawCount < 3) ++rawCount;
    if (rawCount < 3) return false;

    const int32_t value = median(rawValues[0], rawValues[1], rawValues[2]);
    if (sampleCount == 0) minimum = maximum = value;
    else {
      if (value < minimum) minimum = value;
      if (value > maximum) maximum = value;
    }
    sum += value; // Twenty signed 24-bit conversions fit in int32_t.
    if (++sampleCount < WINDOW_SAMPLES) return false;

    const SampleSummary current(sum / WINDOW_SAMPLES,
                                static_cast<uint32_t>(maximum - minimum));
    const uint32_t rawRange = static_cast<uint32_t>(rawMaximum - rawMinimum);
    const bool ready = completedWindows != 0;
    if (ready) {
      const int64_t difference = static_cast<int64_t>(current.mean) - previous.mean;
      result.mean = static_cast<int32_t>((static_cast<int64_t>(current.mean) + previous.mean) / 2);
      result.spread = current.spread > previous.spread ? current.spread : previous.spread;
      result.movement = difference < 0 ? static_cast<uint32_t>(-difference)
                                       : static_cast<uint32_t>(difference);
      pairRawSpread = rawRange > previousRawSpread ? rawRange : previousRawSpread;
    }
    previous = current;
    previousRawSpread = rawRange;
    if (completedWindows < 2) ++completedWindows;
    sampleCount = 0;
    sum = 0;
    rawWindowStarted = false;
    return ready;
  }

  SampleSummary summary() const { return result; }
  uint8_t count() const { return sampleCount; }
  uint8_t windowCount() const { return completedWindows; }
  uint32_t rawSpread() const { return pairRawSpread; }

private:
  static int32_t median(int32_t a, int32_t b, int32_t c) {
    if (a > b) { const int32_t saved = a; a = b; b = saved; }
    if (b > c) { const int32_t saved = b; b = c; c = saved; }
    if (a > b) b = a;
    return b;
  }

  int32_t rawValues[3] = {};
  int32_t sum = 0;
  int32_t minimum = 0;
  int32_t maximum = 0;
  int32_t rawMinimum = 0;
  int32_t rawMaximum = 0;
  SampleSummary previous;
  SampleSummary result;
  uint32_t previousRawSpread = 0;
  uint32_t pairRawSpread = 0;
  uint8_t rawCount = 0;
  uint8_t nextRaw = 0;
  uint8_t sampleCount = 0;
  uint8_t completedWindows = 0;
  bool rawWindowStarted = false;
};

#endif
