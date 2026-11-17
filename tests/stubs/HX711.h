#ifndef TEST_HX711_H
#define TEST_HX711_H

#include "Arduino.h"
#include <cassert>

class HX711 {
  float factor = 1.0f;
  long offset = 0;
  unsigned long nextReadyAt = 0;

public:
  bool connected = true;
  long raw = 1000;
  unsigned long samplePeriodMs = 100;
  unsigned int readCount = 0;
  uint8_t dataPin = 255;
  uint8_t clockPin = 255;

  void begin(uint8_t data, uint8_t clock) {
    dataPin = data;
    clockPin = clock;
  }
  bool is_ready() { return connected && millis() >= nextReadyAt; }
  long read() {
    // A blocking read on a conversion that is not ready fails the test immediately.
    assert(is_ready());
    ++readCount;
    nextReadyAt = millis() + samplePeriodMs;
    return raw;
  }
  void set_scale(float value) { factor = value; }
  float get_scale() { return factor; }
  void set_offset(long value) { offset = value; }
  long get_offset() { return offset; }
};

#endif
