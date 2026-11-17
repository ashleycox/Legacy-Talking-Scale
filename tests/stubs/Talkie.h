#ifndef TEST_TALKIE_H
#define TEST_TALKIE_H

#include "Arduino.h"
#include <cassert>
#include <deque>
#include <vector>

bool testVocabularyWord(const uint8_t *word);

class Talkie {
  struct PendingWord { const uint8_t *data; unsigned long duration; };
  std::deque<PendingWord> pending;
  unsigned long remaining = 0;
  unsigned long updatedAt = 0;
  bool active = false;

  void start(const PendingWord &word) {
    WordDataPointer = word.data;
    remaining = word.duration;
    if (testVocabularyWord(word.data)) words.push_back({word.data, millis()});
  }
  void update() {
    const unsigned long elapsed = millis() - updatedAt;
    updatedAt = millis();
    if (!active || NanoSpeechOutput::frozen) return;
    unsigned long available = elapsed;
    while (active && available >= remaining) {
      available -= remaining;
      if (pending.empty()) {
        active = false;
        WordDataPointer = nullptr;
        break;
      }
      const PendingWord next = pending.front();
      pending.pop_front();
      start(next);
    }
    if (active) remaining -= available;
  }

public:
  struct StartedWord { const uint8_t *data; unsigned long at; };
  bool invertedOutput;
  uint8_t NonInvertedOutputPin = 3;
  uint8_t InvertedOutputPin = 0;
  const uint8_t *WordDataPointer = nullptr;
  unsigned long wordDurationMs = 150;
  std::vector<StartedWord> words;
  std::vector<unsigned long> terminationTimes;

  Talkie(bool, bool inverted) : invertedOutput(inverted) {}
  bool isTalking() { update(); return active; }
  uint8_t getNumberOfWords() { update(); return active ? 1 + pending.size() : 0; }
  int8_t sayQ(const uint8_t *word) {
    update();
    assert(pending.size() < 24); // A full Talkie FIFO would block the main loop.
    const unsigned long duration = testVocabularyWord(word) ? wordDurationMs
                                  : word[0] == 0xf0 ? 25 : 1000;
    if (!active) {
      active = true;
      NanoSpeechOutput::frozen = false;
      updatedAt = millis();
      start({word, duration});
    } else pending.push_back({word, duration});
    return static_cast<int8_t>(24 - pending.size());
  }
  void setPtr(const uint8_t *word) {
    update();
    assert(NanoSpeechOutput::frozen);
    assert(word[0] == 0x0f); // Force the held silent word to its stop frame.
    WordDataPointer = word;
    remaining = 25;
  }
  void terminate() {
    assert(!testInterruptsEnabled);
    assert(NanoSpeechOutput::duty == 0); // Release D3 only after the bias reaches zero.
    assert(active);
    terminationTimes.push_back(millis());
    active = false;
    pending.clear();
    WordDataPointer = nullptr;
  }
};

#endif
