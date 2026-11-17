#ifndef TEST_TALKIE_H
#define TEST_TALKIE_H

#include "Arduino.h"
#include <cassert>
#include <vector>

class Talkie {
  unsigned long wordEndsAt = 0;

public:
  struct StartedWord {
    const uint8_t *data;
    unsigned long at;
  };
  bool invertedOutput;
  unsigned long wordDurationMs = 150;
  std::vector<StartedWord> words;
  std::vector<unsigned long> terminationTimes;

  Talkie(bool, bool inverted) : invertedOutput(inverted) {}
  bool isTalking() { return millis() < wordEndsAt; }
  void sayQ(const uint8_t *word) {
    // The firmware promises never to fill Talkie's internal queue and block.
    assert(!isTalking());
    words.push_back({word, millis()});
    wordEndsAt = millis() + wordDurationMs;
  }
  void terminate() {
    assert(!testInterruptsEnabled); // Reset must be protected from the audio ISR.
    assert(isTalking()); // No hardware reset before the first word initializes D3.
    terminationTimes.push_back(millis());
    wordEndsAt = millis();
  }
};

#endif
