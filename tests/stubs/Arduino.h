#ifndef TEST_ARDUINO_H
#define TEST_ARDUINO_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

#define PROGMEM
#define F(text) text

const uint8_t LOW = 0;
const uint8_t HIGH = 1;
const uint8_t INPUT = 0;
const uint8_t INPUT_PULLUP = 2;

using std::max;

unsigned long millis();
void pinMode(uint8_t pin, uint8_t mode);
int digitalRead(uint8_t pin);

class TestSerial {
public:
  unsigned long baud = 0;
  void begin(unsigned long value) { baud = value; }
  template <typename T> void print(T) {}
  template <typename T> void println(T) {}
  template <typename T> void println(T, int) {}
};

extern TestSerial Serial;

#endif
