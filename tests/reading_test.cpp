#include "../Talking_Scale/ScaleReading.h"

#include <assert.h>
#include <stdio.h>

void expectReading(float grams, bool negative, uint32_t whole,
                   uint8_t tenths, uint8_t hundredths) {
  SpokenReading reading;
  assert(splitReading(grams, reading));
  assert(reading.negative == negative);
  assert(reading.whole == whole);
  assert(reading.tenths == tenths);
  assert(reading.hundredths == hundredths);
}

int main() {
  // Distinct decimal digits expose the original repeated-tenths bug.
  expectReading(1.27f, false, 1, 2, 7);
  expectReading(1.03f, false, 1, 0, 3);
  expectReading(0.01f, false, 0, 0, 1);
  expectReading(-0.01f, true, 0, 0, 1);
  expectReading(-1.27f, true, 1, 2, 7);
  expectReading(2.50f, false, 2, 5, 0);
  expectReading(10.00f, false, 10, 0, 0);

  // Rounding must propagate across decimal places and whole grams.
  expectReading(1.994f, false, 1, 9, 9);
  expectReading(1.996f, false, 2, 0, 0);
  expectReading(-1.996f, true, 2, 0, 0);
  expectReading(0.005f, false, 0, 0, 1);
  expectReading(-0.005f, true, 0, 0, 1);
  expectReading(0.004f, false, 0, 0, 0);
  expectReading(-0.004f, false, 0, 0, 0);
  expectReading(-0.0f, false, 0, 0, 0);

  // Every hundredth in the working range must survive float conversion.
  for (uint32_t value = 0; value <= 500; ++value) {
    const float grams = static_cast<float>(value) / 100.0f;
    expectReading(grams, false, value / 100, (value / 10) % 10,
                  value % 10);
    expectReading(-grams, value != 0, value / 100, (value / 10) % 10,
                  value % 10);
  }

  SpokenReading reading;
  assert(splitReading(999999.0f, reading));
  assert(splitReading(nextafterf(1000000.0f, 0.0f), reading));
  assert(!splitReading(1000000.0f, reading));
  assert(!splitReading(-1000000.0f, reading));
  assert(!splitReading(999999.99f, reading));
  assert(!splitReading(NAN, reading));
  assert(!splitReading(INFINITY, reading));
  assert(!splitReading(-INFINITY, reading));
  puts("Reading tests passed.");
}
