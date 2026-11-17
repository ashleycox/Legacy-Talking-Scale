#include "../Talking_Scale/SpeechPlayback.h"

#include <cassert>
#include <cstdio>
#include <deque>
#include <vector>

struct FakeOutput {
  static bool running;
  static bool attached;
  static bool atomicActive;
  static uint8_t duty;
  static uint32_t now;
  static unsigned touches;
  static std::vector<uint8_t> audibleDuty;

  static void reset(uint32_t time) {
    running = attached = atomicActive = false;
    duty = 0;
    now = time;
    touches = 0;
    audibleDuty.clear();
  }
  static void freeze() { ++touches; running = false; }
  static void resume() { ++touches; running = true; }
  static void attach(uint8_t value) {
    ++touches;
    assert(!running && value == 0);
    attached = true;
    duty = value;
    audibleDuty.push_back(value);
  }
  static void setDuty(uint8_t value) {
    ++touches;
    duty = value;
    if (attached) audibleDuty.push_back(value);
  }
  static uint8_t currentDuty() { return duty; }
  template<class Action> static void atomic(Action action) {
    assert(!atomicActive);
    atomicActive = true;
    action();
    atomicActive = false;
  }
};
bool FakeOutput::running;
bool FakeOutput::attached;
bool FakeOutput::atomicActive;
uint8_t FakeOutput::duty;
uint32_t FakeOutput::now;
unsigned FakeOutput::touches;
std::vector<uint8_t> FakeOutput::audibleDuty;

struct FakeEngine {
  struct Word { const uint8_t *data; uint32_t at; };
  uint8_t NonInvertedOutputPin = 255;
  uint8_t InvertedOutputPin = 0;
  bool active = false;
  bool coefficientsCleared = false;
  uint32_t lastAt = 0;
  uint32_t remaining = 0;
  const uint8_t *current = nullptr;
  std::deque<const uint8_t *> fifo;
  std::vector<Word> words;
  unsigned terminations = 0;
  unsigned naturalShutdowns = 0;
  unsigned mutedRealStarts = 0;
  size_t maximumQueued = 0;

  static bool realWord(const uint8_t *data) {
    return data[0] != 0 && data[0] != 0xf0 && data[0] != 0x0f;
  }
  void beginWord(const uint8_t *data) {
    current = data;
    if (data[0] == 0xf0) remaining = 25;
    else if (data[0] == 0x0f) remaining = 1;
    else if (data[0] == 0) {
      uint8_t count = 0;
      while (data[count] == 0) ++count;
      remaining = static_cast<uint32_t>(count) * 50;
    } else {
      remaining = 80;
      if (!FakeOutput::attached) ++mutedRealStarts;
      assert(coefficientsCleared);
      words.push_back({data, FakeOutput::now});
    }
  }
  void sayQ(const uint8_t *data) {
    if (!active) {
      active = true;
      lastAt = FakeOutput::now;
      FakeOutput::running = true;
      // Stock Talkie would connect D3 here if its output flag were enabled.
      assert(NonInvertedOutputPin == 0 && InvertedOutputPin == 0);
      assert(!FakeOutput::attached);
      beginWord(data);
    } else {
      assert(fifo.size() < 24);
      fifo.push_back(data);
      if (fifo.size() > maximumQueued) maximumQueued = fifo.size();
    }
  }
  bool isTalking() const { return active; }
  uint8_t getNumberOfWords() const {
    return active ? static_cast<uint8_t>(fifo.size() + 1) : 0;
  }
  void setPtr(const uint8_t *data) {
    assert(FakeOutput::atomicActive && !FakeOutput::running);
    assert(data[0] == 0x0f);
    current = data;
    remaining = 1;
  }
  void terminate() {
    assert(active && FakeOutput::atomicActive && !FakeOutput::running);
    assert(FakeOutput::duty == 0);
    ++terminations;
    active = false;
    fifo.clear();
    FakeOutput::attached = false;
  }
  void advance(uint32_t time) {
    uint32_t elapsed = time - lastAt;
    lastAt = time;
    if (!FakeOutput::running || !active) return;
    while (active && elapsed >= remaining) {
      elapsed -= remaining;
      // A real LPC stop frame clears coefficients before popping the FIFO.
      coefficientsCleared = true;
      if (fifo.empty()) {
        active = false;
        FakeOutput::running = FakeOutput::attached = false;
        ++naturalShutdowns;
      } else {
        const uint8_t *next = fifo.front();
        fifo.pop_front();
        beginWord(next);
      }
    }
    if (active) {
      remaining -= elapsed;
      FakeOutput::setDuty(128); // Silent/rest output and simplified centred speech.
    }
  }
};

struct Fixture {
  FakeEngine engine;
  SpeechPlayback<FakeEngine, FakeOutput> playback;
  uint32_t now;
  explicit Fixture(uint32_t time = 0) : playback(engine), now(time) {
    FakeOutput::reset(time);
  }
  void advance(uint32_t duration) {
    while (duration--) {
      ++now;
      FakeOutput::now = now;
      engine.advance(now);
      playback.service(now);
    }
  }
  void finish() {
    const uint32_t started = now;
    while (playback.busy() && now - started < 5000) advance(1);
    assert(!playback.busy());
  }
};

const uint8_t one[] = {0x81};
const uint8_t two[] = {0x82};
const uint8_t three[] = {0x83};

void testNaturalPhrase() {
  Fixture fixture;
  const uint8_t *words[] = {one, two};
  fixture.playback.stop(fixture.now);
  assert(FakeOutput::touches == 0); // Idle stop never touches uninitialized pins.
  assert(fixture.playback.beginPhrase(words, 2, fixture.now));
  assert(fixture.engine.getNumberOfWords() == 5);
  fixture.advance(24);
  assert(!FakeOutput::attached && fixture.engine.words.empty());
  fixture.finish();
  assert(fixture.playback.completed());
  assert(fixture.engine.words.size() == 2);
  assert(fixture.engine.words[0].data == one && fixture.engine.words[1].data == two);
  assert(fixture.engine.terminations == 1 && fixture.engine.naturalShutdowns == 0);
  assert(fixture.engine.mutedRealStarts == 0);
  assert(FakeOutput::duty == 0 && !FakeOutput::attached && !FakeOutput::running);
  for (size_t i = 1; i < FakeOutput::audibleDuty.size(); ++i) {
    const int difference = static_cast<int>(FakeOutput::audibleDuty[i]) -
                           static_cast<int>(FakeOutput::audibleDuty[i - 1]);
    assert(difference >= -2 && difference <= 2);
  }
}

void testCancellationAndReplacement() {
  Fixture fixture;
  const uint8_t *oldWords[] = {one, two};
  const uint8_t *newWords[] = {three};
  fixture.playback.beginPhrase(oldWords, 2, fixture.now);
  while (fixture.engine.words.empty()) fixture.advance(1);
  fixture.advance(10);
  assert(fixture.playback.beginPhrase(newWords, 1, fixture.now));
  assert(!FakeOutput::running); // Cancellation stops sample generation immediately.
  fixture.finish();
  assert(fixture.playback.completed()); // Only the replacement completed.
  assert(fixture.engine.words.size() == 2);
  assert(fixture.engine.words[0].data == one && fixture.engine.words[1].data == three);
  assert(fixture.engine.terminations == 2 && fixture.engine.naturalShutdowns == 0);

  fixture.playback.beginPhrase(oldWords, 2, fixture.now);
  while (fixture.engine.words.size() == 2) fixture.advance(1);
  fixture.playback.stop(fixture.now);
  assert(fixture.playback.busy()); // Bias is still ramping down.
  fixture.finish();
  assert(!fixture.playback.completed());
}

void testMutedCancellation() {
  Fixture fixture;
  const uint8_t *words[] = {one};
  fixture.playback.beginPhrase(words, 1, fixture.now);
  fixture.advance(10);
  fixture.playback.stop(fixture.now);
  assert(!fixture.playback.busy() && !fixture.playback.completed());
  assert(fixture.engine.words.empty() && fixture.engine.terminations == 1);
  assert(!FakeOutput::attached);
  fixture.playback.beginPhrase(words, 1, fixture.now);
  fixture.finish();
  assert(fixture.playback.completed() && fixture.engine.words.size() == 1);
}

void testQueueBoundAndRollover() {
  Fixture fixture(UINT32_MAX - 15);
  const uint8_t *words[20];
  for (uint8_t i = 0; i < 20; ++i) words[i] = one;
  assert(fixture.playback.beginPhrase(words, 20, fixture.now));
  assert(fixture.engine.maximumQueued == 22);
  fixture.finish();
  assert(fixture.playback.completed() && fixture.engine.words.size() == 20);
  assert(!fixture.playback.beginPhrase(words, 21, fixture.now));
  assert(!fixture.playback.beginPhrase(nullptr, 1, fixture.now));
  words[0] = nullptr;
  assert(!fixture.playback.beginPhrase(words, 1, fixture.now));
}

void testStarvedPreroll() {
  Fixture fixture;
  const uint8_t *words[] = {one};
  fixture.playback.beginPhrase(words, 1, fixture.now);
  fixture.now += 500;
  FakeOutput::now = fixture.now;
  fixture.engine.advance(fixture.now);
  fixture.playback.service(fixture.now);
  assert(!fixture.playback.busy() && !fixture.playback.completed());
  assert(fixture.engine.words.empty() && !FakeOutput::attached);
}

void testDelayedRamp() {
  Fixture fixture;
  const uint8_t *words[] = {one};
  fixture.playback.beginPhrase(words, 1, fixture.now);
  while (!FakeOutput::attached) fixture.advance(1);
  assert(FakeOutput::duty == 0 && !FakeOutput::running);
  fixture.now += 50;
  FakeOutput::now = fixture.now;
  fixture.engine.advance(fixture.now);
  fixture.playback.service(fixture.now);
  assert(FakeOutput::duty == 2); // Catching up must not jump to the elapsed-time target.
  fixture.finish();
  assert(fixture.playback.completed());
  for (size_t i = 1; i < FakeOutput::audibleDuty.size(); ++i) {
    const int difference = static_cast<int>(FakeOutput::audibleDuty[i]) -
                           static_cast<int>(FakeOutput::audibleDuty[i - 1]);
    assert(difference >= -2 && difference <= 2);
  }
}

int main() {
  testNaturalPhrase();
  testCancellationAndReplacement();
  testMutedCancellation();
  testQueueBoundAndRollover();
  testStarvedPreroll();
  testDelayedRamp();
  puts("Speech playback tests passed.");
}
