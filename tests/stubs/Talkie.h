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

  Talkie(bool, bool inverted) : invertedOutput(inverted) {}
  bool isTalking() { return millis() < wordEndsAt; }
  void sayQ(const uint8_t *word) {
    // The firmware promises never to fill Talkie's internal queue and block.
    assert(!isTalking());
    words.push_back({word, millis()});
    wordEndsAt = millis() + wordDurationMs;
  }
  // Real Talkie discards queued words and lets its current word finish.
  // This stub has no internal queue because the sketch feeds only idle words.
  void stop() {}
};

#endif
