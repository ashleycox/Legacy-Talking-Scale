#ifndef SPEECH_PLAYBACK_H
#define SPEECH_PLAYBACK_H

#include <stdint.h>

#if defined(__AVR__)
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <util/atomic.h>
#define SPEECH_PLAYBACK_FLASH PROGMEM

// D3 / OC2B on the fixed 16 MHz ATmega328P hardware. Timer 0 is untouched.
struct NanoSpeechOutput {
  static void freeze() { TIMSK1 = 0; }
  static void resume() {
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
      TCNT1 = 0;
      TIFR1 = _BV(OCF1B); // Discard a compare that elapsed during the ramp.
      TIMSK1 = _BV(OCIE1B);
    }
  }
  static void attach(uint8_t duty) {
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
      // Write OCR in normal mode so an old double-buffered speech value cannot
      // become the first PWM pulse when reconnecting the speaker.
      TCCR2B = 0;
      TCCR2A = 0;
      TCNT2 = 0;
      OCR2B = duty;
      PORTD &= static_cast<uint8_t>(~_BV(PD3));
      DDRD |= _BV(DDD3);
      TCCR2A = _BV(WGM21) | _BV(WGM20) | _BV(COM2B1);
      TCCR2B = _BV(CS20);
      TIMSK2 = 0;
    }
  }
  static void setDuty(uint8_t duty) { OCR2B = duty; }
  static uint8_t currentDuty() { return OCR2B; }
  template<class Action> static void atomic(Action action) {
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { action(); }
  }
};
#else
#define SPEECH_PLAYBACK_FLASH
#endif

// Talkie 1.4's public FIFO and output flags let this adapter keep its normal
// shutdown away from audible audio. Silent words bracket each complete phrase;
// the speaker is connected only after synthesis has settled, and released only
// after its PWM bias reaches zero. No additional ISR or hardware is required.
template<class Engine, class Output> class SpeechPlayback {
public:
  static const uint8_t MAX_WORDS = 20;
  static const uint32_t RAMP_MS = 64;

  explicit SpeechPlayback(Engine &speechEngine) : engine(speechEngine) {}

  bool beginPhrase(const uint8_t *const *words, uint8_t count, uint32_t now) {
    if (!words || count == 0 || count > MAX_WORDS) return false;
    for (uint8_t i = 0; i < count; ++i) if (!words[i]) return false;
    for (uint8_t i = 0; i < count; ++i) phrase[i] = words[i];
    wordCount = count;
    pending = true;
    finished = false;
    if (state == Idle) launch(now);
    else cancelActive(now);
    return true;
  }

  void stop(uint32_t now) {
    pending = false;
    finished = false;
    if (state != Idle) cancelActive(now);
  }

  bool busy() const { return state != Idle || pending; }
  bool completed() const { return finished; }

  void service(uint32_t now) {
    if (state == Idle) return;
    if (state == Priming || state == PrimeSettling) {
      const uint8_t remaining = engine.getNumberOfWords();
      if (!engine.isTalking() || now - phaseAt >= PRIME_TIMEOUT_MS ||
          remaining < static_cast<uint8_t>(wordCount + 2)) {
        abortMuted();
        return;
      }
      if (state == Priming && remaining == static_cast<uint8_t>(wordCount + 2)) {
        state = PrimeSettling;
        phaseAt = now;
      } else if (state == PrimeSettling && now - phaseAt >= PRIME_SETTLE_MS) {
        Output::freeze();
        Output::attach(0);
        engine.NonInvertedOutputPin = 3;
        state = RampingUp;
        phaseAt = now;
      }
      return;
    }
    if (state == RampingUp) {
      const uint32_t elapsed = now - phaseAt;
      const uint8_t target = elapsed < RAMP_MS
          ? static_cast<uint8_t>((elapsed * 128) / RAMP_MS) : 128;
      if (approachDuty(target) == 128) {
        // The stop frame replaces the rest of the silent hold; its normal FIFO
        // transition starts the first real word with cleared coefficients.
        Output::atomic([&]() { engine.setPtr(stopFrame()); });
        Output::resume();
        state = Speaking;
      }
      return;
    }
    if (state == Speaking) {
      if (!engine.isTalking()) {
        // A starved or externally modified engine must not acknowledge speech.
        abortMuted();
      } else if (engine.getNumberOfWords() == 1) {
        naturalEnd = true;
        beginRampDown(now);
      }
      return;
    }
    if (state == RampingDown) {
      const uint32_t elapsed = now - phaseAt;
      const uint8_t target = elapsed < RAMP_MS
          ? static_cast<uint8_t>(rampFrom - (elapsed * rampFrom) / RAMP_MS) : 0;
      if (approachDuty(target) == 0) {
        // Allow Timer2's double buffer to reach zero before disconnecting D3.
        state = ZeroSettling;
        phaseAt = now;
      }
      return;
    }
    if (state == ZeroSettling && now - phaseAt >= 1) finish(now);
  }

private:
  enum State : uint8_t { Idle, Priming, PrimeSettling, RampingUp, Speaking,
                         RampingDown, ZeroSettling };
  static const uint32_t PRIME_TIMEOUT_MS = 250;
  static const uint32_t PRIME_SETTLE_MS = 2;
  Engine &engine;
  const uint8_t *phrase[MAX_WORDS] = {};
  uint32_t phaseAt = 0;
  uint8_t wordCount = 0;
  uint8_t rampFrom = 0;
  State state = Idle;
  bool pending = false;
  bool naturalEnd = false;
  bool finished = false;

  static const uint8_t *primeWord() {
    // One silent 25 ms frame, followed by a stop frame that clears coefficients.
    static const uint8_t data[] SPEECH_PLAYBACK_FLASH = {0xf0, 0};
    return data;
  }
  static const uint8_t *silentHold() {
    // A long hold leaves ample time for polling before Talkie can shut down.
    static const uint8_t data[] SPEECH_PLAYBACK_FLASH = {
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0x0f, 0
    };
    return data;
  }
  static const uint8_t *stopFrame() {
    static const uint8_t data[] SPEECH_PLAYBACK_FLASH = {0x0f, 0};
    return data;
  }

  static uint8_t approachDuty(uint8_t target) {
    const uint8_t current = Output::currentDuty();
    // If polling is delayed, extend the ramp instead of making a large DC step.
    if (target > current && target - current > 2) target = current + 2;
    else if (target < current && current - target > 2) target = current - 2;
    Output::setDuty(target);
    return target;
  }

  void launch(uint32_t now) {
    pending = false;
    naturalEnd = false;
    finished = false;
    engine.NonInvertedOutputPin = 0;
    engine.InvertedOutputPin = 0;
    // At most 22 pending words fit the stock 24-entry FIFO, including both
    // silent holds. Queue everything before the preroll stop frame is reached.
    engine.sayQ(primeWord());
    engine.sayQ(silentHold());
    for (uint8_t i = 0; i < wordCount; ++i) engine.sayQ(phrase[i]);
    engine.sayQ(silentHold());
    state = Priming;
    phaseAt = now;
  }

  void cancelActive(uint32_t now) {
    naturalEnd = false;
    if (state == Priming || state == PrimeSettling) {
      // D3 is still disconnected, so there is no speaker bias to fade.
      terminateAtZero();
      state = Idle;
      if (pending) launch(now);
    } else if (state != RampingDown && state != ZeroSettling) {
      beginRampDown(now);
    }
  }

  void beginRampDown(uint32_t now) {
    Output::freeze();
    rampFrom = Output::currentDuty();
    state = RampingDown;
    phaseAt = now;
  }

  void terminateAtZero() {
    Output::freeze();
    Output::setDuty(0);
    Output::atomic([&]() {
      if (engine.isTalking()) engine.terminate();
    });
  }

  void abortMuted() {
    // This path is only expected if the engine is modified outside the adapter
    // or polling stalls beyond its reserved silent words.
    terminateAtZero();
    state = Idle;
    pending = false;
    naturalEnd = false;
    finished = false;
  }

  void finish(uint32_t now) {
    terminateAtZero();
    state = Idle;
    finished = naturalEnd && !pending;
    naturalEnd = false;
    if (pending) launch(now);
  }
};

#undef SPEECH_PLAYBACK_FLASH
#endif
