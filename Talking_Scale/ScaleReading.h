#ifndef SCALE_READING_H
#define SCALE_READING_H

#include <math.h>
#include <stdint.h>

struct SpokenReading {
  bool negative;
  uint32_t whole;
  uint8_t tenths;
  uint8_t hundredths;
};

inline bool splitReading(float grams, SpokenReading &result) {
  if (!isfinite(grams)) {
    return false;
  }

  // Round the magnitude once so a carry also reaches the whole grams.
  const float roundedHundredths = floorf(fabsf(grams) * 100.0f + 0.5f);
  if (roundedHundredths >= 100000000.0f) {
    return false;
  }

  const uint32_t hundredths = static_cast<uint32_t>(roundedHundredths);
  result.negative = grams < 0.0f && hundredths != 0;
  result.whole = hundredths / 100;
  result.tenths = (hundredths / 10) % 10;
  result.hundredths = hundredths % 10;
  return true;
}

#endif
