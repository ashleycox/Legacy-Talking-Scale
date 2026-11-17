#ifndef SCALE_FILTER_H
#define SCALE_FILTER_H

#include <math.h>
#include <stdint.h>

class ScaleFilter {
public:
  static const uint8_t WINDOW_SAMPLES = 20;
  static const uint32_t UNCALIBRATED_MAX_SPREAD = 4096;

  void reset() {
    sum = 0;
    sampleCount = 0;
    nextSample = 0;
    rawCount = 0;
    nextRaw = 0;
    sampleAt = 0;
  }

  void add(int32_t raw, uint32_t now) {
    rawValues[nextRaw] = raw;
    nextRaw = (nextRaw + 1) % 3;
    sampleAt = now;
    if (rawCount < 3) ++rawCount;
    // Prime all three inputs before accepting any median, including at startup.
    if (rawCount < 3) return;

    const int32_t filtered = median(rawValues[0], rawValues[1], rawValues[2]);
    if (sampleCount == WINDOW_SAMPLES) {
      sum -= samples[nextSample];
    } else {
      ++sampleCount;
    }
    samples[nextSample] = filtered;
    sum += filtered; // Twenty signed 24-bit HX711 conversions fit in int32_t.
    nextSample = (nextSample + 1) % WINDOW_SAMPLES;
  }

  bool ready() const { return sampleCount == WINDOW_SAMPLES; }
  uint8_t count() const { return sampleCount; }
  int32_t mean() const { return sampleCount ? sum / sampleCount : 0; }

  uint32_t spread() const {
    if (!sampleCount) return 0;
    int32_t minimum = samples[0];
    int32_t maximum = samples[0];
    for (uint8_t i = 1; i < sampleCount; ++i) {
      if (samples[i] < minimum) minimum = samples[i];
      if (samples[i] > maximum) maximum = samples[i];
    }
    // HX711 inputs are signed 24-bit; their full span also fits in int32_t.
    return static_cast<uint32_t>(maximum - minimum);
  }

  uint32_t meanSpread() const {
    if (!ready()) return 0;
    int32_t minimum = 0;
    int32_t maximum = 0;
    // nextSample identifies the oldest value once the ring is full. Grouping
    // physical array slots would mix the ends of the time window after wrap.
    uint8_t index = nextSample;
    for (uint8_t block = 0; block < 4; ++block) {
      int32_t blockSum = 0;
      for (uint8_t sample = 0; sample < 5; ++sample) {
        blockSum += samples[index]; // Five signed 24-bit values fit in int32_t.
        if (++index == WINDOW_SAMPLES) index = 0;
      }
      const int32_t blockMean = blockSum / 5;
      if (block == 0) minimum = maximum = blockMean;
      else {
        if (blockMean < minimum) minimum = blockMean;
        if (blockMean > maximum) maximum = blockMean;
      }
    }
    return static_cast<uint32_t>(maximum - minimum);
  }

  bool stable(float factor, bool calibrated) const {
    if (!ready()) return false;
    float maximumSpread = UNCALIBRATED_MAX_SPREAD;
    if (calibrated) {
      if (!isfinite(factor) || factor == 0.0f) return false;
      maximumSpread = fabsf(factor) * 0.01f;
      if (maximumSpread < 2.0f) maximumSpread = 2.0f;
    }
    // Judge movement between short averaged blocks, not every noisy ADC sample.
    // spread() remains available as the unaveraged filtered-noise diagnostic.
    return static_cast<float>(meanSpread()) <= maximumSpread;
  }

  int32_t latestRaw() const {
    return rawCount ? rawValues[(nextRaw + 2) % 3] : 0;
  }
  uint32_t lastSampleAt() const { return sampleAt; }

private:
  static int32_t median(int32_t a, int32_t b, int32_t c) {
    if (a > b) { const int32_t saved = a; a = b; b = saved; }
    if (b > c) { const int32_t saved = b; b = c; c = saved; }
    if (a > b) b = a;
    return b;
  }

  int32_t samples[WINDOW_SAMPLES];
  int32_t rawValues[3];
  int32_t sum = 0;
  uint32_t sampleAt = 0;
  uint8_t sampleCount = 0;
  uint8_t nextSample = 0;
  uint8_t rawCount = 0;
  uint8_t nextRaw = 0;
};

#endif
