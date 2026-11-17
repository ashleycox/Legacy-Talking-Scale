#include "../Talking_Scale/SpeechVocabulary.h"
#include "../Talking_Scale/ModeVocabulary.h"
#include "../Talking_Scale/SpeechPlayback.h"

#include <cassert>
#include <cstdio>
#include <deque>
#include <vector>

// Read the real LPC fields rather than assigning every word a fake duration.
// Talkie consumes each byte least-significant bit first, assembling fields MSB first.
class LpcCursor {
public:
  enum Frame { Stop, Rest, Voiced, Unvoiced };
  explicit LpcCursor(const uint8_t *data) : bytes(data) {}
  Frame next() {
    const uint8_t energy = bits(4);
    if (energy == 15) return Stop;
    if (energy == 0) return Rest;
    const bool repeat = bits(1) != 0;
    const bool voiced = bits(6) != 0;
    if (!repeat) {
      bits(5); bits(5); bits(4); bits(4);
      if (voiced) { bits(4); bits(4); bits(4); bits(3); bits(3); bits(3); }
    }
    return voiced ? Voiced : Unvoiced;
  }

private:
  const uint8_t *bytes;
  uint16_t bit = 0;
  uint8_t bits(uint8_t count) {
    uint8_t value = 0;
    while (count--) {
      value = static_cast<uint8_t>((value << 1) | ((bytes[bit / 8] >> (bit % 8)) & 1));
      ++bit;
      assert(bit < 4096); // All tested vocabulary and boundary words are much shorter.
    }
    return value;
  }
};

struct WordAnalysis {
  unsigned frames = 0;
  unsigned voiced = 0;
  unsigned unvoiced = 0;
};

WordAnalysis analyse(const uint8_t *data) {
  LpcCursor cursor(data);
  WordAnalysis result;
  for (;;) {
    const LpcCursor::Frame frame = cursor.next();
    if (frame == LpcCursor::Stop) return result;
    ++result.frames;
    if (frame == LpcCursor::Voiced) ++result.voiced;
    if (frame == LpcCursor::Unvoiced) ++result.unvoiced;
  }
}

bool isTestWord(const uint8_t *word) {
  return word == spERROR || word == spCALIBRATE || word == spFAIL;
}

struct LpcOutput {
  static bool running;
  static bool attached;
  static bool atomicActive;
  static uint8_t duty;
  static void reset() { running = attached = atomicActive = false; duty = 0; }
  static void freeze() { running = false; }
  static void resume() { running = true; }
  static void attach(uint8_t value) {
    assert(!running && value == 0);
    attached = true;
    duty = value;
  }
  static void setDuty(uint8_t value) { duty = value; }
  static uint8_t currentDuty() { return duty; }
  template<class Action> static void atomic(Action action) {
    assert(!atomicActive);
    atomicActive = true;
    action();
    atomicActive = false;
  }
};
bool LpcOutput::running;
bool LpcOutput::attached;
bool LpcOutput::atomicActive;
uint8_t LpcOutput::duty;

struct LpcEngine {
  struct HeardWord {
    const uint8_t *data;
    uint32_t firstFrameAt;
    uint32_t stoppedAt;
    unsigned frames;
  };
  uint8_t NonInvertedOutputPin = 255;
  uint8_t InvertedOutputPin = 0;
  bool active = false;
  bool coefficientsCleared = false;
  unsigned naturalShutdowns = 0;
  std::vector<HeardWord> heard;

  void sayQ(const uint8_t *data) {
    if (active) {
      assert(fifo.size() < 24);
      fifo.push_back(data);
    } else {
      assert(NonInvertedOutputPin == 0 && InvertedOutputPin == 0);
      assert(!LpcOutput::attached);
      setCurrent(data);
      decodeFrame(); // sayQ parses its first frame before enabling the sample timer.
      fifo.clear();
      active = true;
      samplesToFrame = 200;
      LpcOutput::running = true;
    }
  }
  bool isTalking() const { return active; }
  uint8_t getNumberOfWords() const {
    return active ? static_cast<uint8_t>(1 + fifo.size()) : 0;
  }
  void setPtr(const uint8_t *data) {
    assert(LpcOutput::atomicActive && !LpcOutput::running);
    setCurrent(data);
  }
  void terminate() {
    assert(LpcOutput::atomicActive && !LpcOutput::running && LpcOutput::duty == 0);
    assert(active);
    active = false;
    fifo.clear();
    LpcOutput::attached = false;
  }
  void advance(uint32_t time) {
    const uint32_t elapsed = time - lastAt;
    lastAt = now = time;
    if (!active || !LpcOutput::running) return;
    for (uint32_t sample = 0; sample < elapsed * 8; ++sample) {
      // Audio amplitude is irrelevant here; track the actual 8 kHz/40 Hz scheduling.
      LpcOutput::setDuty(128);
      if (--samplesToFrame == 0) {
        samplesToFrame = 200;
        if (!current) {
          active = false;
          LpcOutput::running = LpcOutput::attached = false;
          ++naturalShutdowns;
          return;
        }
        decodeFrame();
      }
    }
  }

private:
  std::deque<const uint8_t *> fifo;
  const uint8_t *current = nullptr;
  LpcCursor cursor = LpcCursor(nullptr);
  uint32_t lastAt = 0;
  uint32_t now = 0;
  uint16_t samplesToFrame = 200;
  bool wordHasFrames = false;

  void setCurrent(const uint8_t *data) {
    current = data;
    cursor = LpcCursor(data);
    wordHasFrames = false;
  }
  void decodeFrame() {
    const LpcCursor::Frame frame = cursor.next();
    if (frame == LpcCursor::Stop) {
      coefficientsCleared = true;
      if (isTestWord(current)) {
        assert(wordHasFrames);
        heard.back().stoppedAt = now;
      }
      const uint8_t *next = nullptr;
      if (!fifo.empty()) { next = fifo.front(); fifo.pop_front(); }
      setCurrent(next);
      return;
    }
    if (isTestWord(current)) {
      assert(LpcOutput::attached && NonInvertedOutputPin == 3);
      if (!wordHasFrames) {
        assert(coefficientsCleared);
        heard.push_back({current, now, 0, 0});
        wordHasFrames = true;
      }
      ++heard.back().frames;
      if (frame == LpcCursor::Voiced || frame == LpcCursor::Unvoiced) {
        coefficientsCleared = false;
      }
    }
  }
};

void expectCompletePhrase(const uint8_t *first, const uint8_t *second) {
  LpcOutput::reset();
  LpcEngine engine;
  SpeechPlayback<LpcEngine, LpcOutput> playback(engine);
  const uint8_t *const words[] = {first, second};
  assert(playback.beginPhrase(words, 2, 0));
  uint32_t now = 0;
  while (playback.busy() && now < 5000) {
    engine.advance(++now);
    playback.service(now);
  }
  assert(!playback.busy() && playback.completed());
  assert(engine.naturalShutdowns == 0 && engine.heard.size() == 2);
  assert(engine.heard[0].data == first && engine.heard[1].data == second);
  for (const LpcEngine::HeardWord &word : engine.heard) {
    const WordAnalysis expected = analyse(word.data);
    assert(word.frames == expected.frames);
    assert(word.stoppedAt - word.firstFrameAt == expected.frames * 25);
  }
  assert(engine.heard[1].firstFrameAt > engine.heard[0].stoppedAt);
  assert(!LpcOutput::attached && LpcOutput::duty == 0);
}

int main() {
  const WordAnalysis error = analyse(spERROR);
  assert(error.frames == 16 && error.voiced == 16 && error.unvoiced == 0);
  assert(analyse(spCALIBRATE).frames == 27);
  const WordAnalysis fail = analyse(spFAIL);
  assert(fail.frames == 22 && fail.unvoiced == 5);
  expectCompletePhrase(spERROR, spCALIBRATE);
  expectCompletePhrase(spCALIBRATE, spFAIL);
  puts("Real LPC calibration speech tests passed.");
}
