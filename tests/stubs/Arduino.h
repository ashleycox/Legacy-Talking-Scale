#ifndef TEST_ARDUINO_H
#define TEST_ARDUINO_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <string>
#include <cassert>
#include "Print.h"

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
extern bool testInterruptsEnabled;

struct NanoSpeechOutput {
  static bool frozen;
  static bool connected;
  static uint8_t duty;
  static void freeze() { frozen = true; }
  static void resume() { frozen = false; }
  static void attach(uint8_t value) { duty = value; connected = true; }
  static void setDuty(uint8_t value) { duty = value; }
  static uint8_t currentDuty() { return duty; }
  template<class Function> static void atomic(Function function) {
    const bool saved = testInterruptsEnabled;
    testInterruptsEnabled = false;
    function();
    testInterruptsEnabled = saved;
  }
};

class TestSerial : public Print {
public:
  unsigned long baud = 0;
  int space = 64;
  std::deque<char> input;
  std::string output;
  void begin(unsigned long value) { baud = value; }
  int available() const { return static_cast<int>(input.size()); }
  int read() { const char value = input.front(); input.pop_front(); return value; }
  int availableForWrite() const { return space; }
  size_t write(uint8_t value) override {
    assert(space > 0); // A serial write beyond the available budget would block.
    --space;
    output.push_back(static_cast<char>(value));
    return 1;
  }
  void advance() { if (space < 64) ++space; }
  void send(const std::string &value) { for (char c : value) input.push_back(c); }
};

extern TestSerial Serial;

#endif
