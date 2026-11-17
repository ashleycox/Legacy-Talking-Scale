/**
  Arduino Talking Scale
  Concept and code by Ashley Cox, with libraries the property of their respective owners
  and distributed in accordance with their respective licenses.
  Permission is granted for the code within to be used for non commercial purposes.
  Please contact for commercial usage rights. More details at https://ashleycox.co.uk
**/

#include <Arduino.h>
#include <EEPROM.h>
#include <Talkie.h>
#include <HX711.h>
#include <util/atomic.h>
#include "ScaleButton.h"
#include "ScaleReading.h"
#include "CalibrationCheck.h"
#include "CalibrationSampler.h"
#include "SpeechVocabulary.h"
#include "ScaleFilter.h"
#include "CalibrationStore.h"
#include "AnnouncementPolicy.h"
#include "ModeVocabulary.h"
#include "SensorHealth.h"
#include "DiagnosticConsole.h"
#include "SpeechPlayback.h"

#if !defined(__AVR_ATmega328P__) || F_CPU != 16000000UL
#error This sketch requires a 16 MHz ATmega328P Nano or Uno with speech on D3.
#endif

const uint8_t BUTTON_PIN = 2;
const uint8_t LOADCELL_DOUT_PIN = 4;
const uint8_t LOADCELL_SCK_PIN = 5;
const float CALIBRATION_WEIGHT_GRAMS = 5.0f;
const unsigned long WEIGHT_PLACEMENT_MS = 2000;
const unsigned long SENSOR_TIMEOUT_MS = 3000;
const unsigned long CALIBRATION_TIMEOUT_MS = 15000;
const unsigned long ZERO_TIMEOUT_MS = 15000;
const unsigned long ZERO_SETTLE_MS = 500;
CalibrationStore<EEPROMClass> calibrationStore(EEPROM);

// Talkie owns timers 1 and 2. Timer 0 supplies millis() for button timing.
// Disable Talkie's default inverted D11 output: the existing speaker uses D3/GND.
Talkie voice(true, false);
SpeechPlayback<Talkie, NanoSpeechOutput> playback(voice);
ScaleButton button;
HX711 scale;
ScaleFilter readingFilter;
CalibrationSampler calibrationSampler;
SensorHealth sensorHealth;
DiagnosticConsole console;
ConsoleCommand pendingCommand = ConsoleCommand::None;
bool diagnosticStream = false;
unsigned long lastDiagnosticAt = 0;
long latestRaw = 0;
AnnouncementPolicy announcements;
bool speakingMeasurement = false;
int32_t spokenValue = 0;

// The longest reading needs 17 words, including its sign and two decimal digits.
const uint8_t MAX_PHRASE_WORDS = 20;
const uint8_t *speechWords[MAX_PHRASE_WORDS];
uint8_t speechLength = 0;
uint8_t nextSpeechWord = 0;
bool speechPaused = false;
bool restartCalibrationPrompt = false;

enum Operation {
  STARTUP_ZERO, WEIGHING, USER_ZERO, CALIBRATION_ZERO,
  CALIBRATION_PROMPT, CALIBRATION_WAIT, CALIBRATION_READ, CALIBRATION_COMMIT
};
enum MenuOption { NO_SELECTION, ZERO_WEIGHT, CALIBRATE, ANNOUNCEMENTS };
Operation operation = STARTUP_ZERO;
MenuOption menuOption = NO_SELECTION;
bool menu = false;
bool calibrated = false;
bool sensorFault = false;
bool recoveryNoticePending = false;
bool readingAvailable = false;
bool readingStable = false;
bool stableWindow = false;
bool zeroTimeoutReported = false;
unsigned long stableSince = 0;
float latestReading = 0.0f;
uint8_t sampleCount = 0;
long calibrationZero = 0;
SampleSummary calibrationBaseline;
SampleSummary calibrationLoaded;
enum class CalibrationStatus : uint8_t {
  Idle, Baseline, Weight, NoSignal, BaselineMoving, WeightMoving,
  InvalidFactor, Saved, WriteFailed, SensorFailed, Cancelled
};
CalibrationStatus calibrationStatus = CalibrationStatus::Idle;
bool calibrationTimedOut = false;
bool streamCalibrationNext = false;
float candidateCalibration = 0.0f;
unsigned long operationStartedAt = 0;
unsigned long placementStartedAt = 0;
unsigned long lastSampleAt = 0;

// Stop synthesis immediately, then let the output bias ramp down before D3 is
// released. A replacement phrase waits for that nonblocking ramp to finish.
void startPhrase() {
  speakingMeasurement = false;
  playback.stop(static_cast<uint32_t>(millis()));
  speechLength = 0;
  nextSpeechWord = 0;
}

void queueWord(const uint8_t *word) {
  if (speechLength < MAX_PHRASE_WORDS) {
    speechWords[speechLength++] = word;
  }
}

bool speechIdle() {
  return nextSpeechWord == speechLength && !playback.busy();
}

void serviceSpeech() {
  const unsigned long now = millis(); // A button action may have just written EEPROM.
  playback.service(static_cast<uint32_t>(now));
  if (speakingMeasurement && speechIdle()) {
    if (playback.completed()) {
      announcements.completed(spokenValue, static_cast<uint32_t>(now));
    }
    speakingMeasurement = false;
  }
  // Queue the complete phrase with silent boundary words. Its maximum size fits
  // Talkie's FIFO, preventing both starvation between words and a blocking fill.
  if (!speechPaused && nextSpeechWord < speechLength && !playback.busy()) {
    if (playback.beginPhrase(speechWords, speechLength, static_cast<uint32_t>(now))) {
      nextSpeechWord = speechLength;
    }
  }
}

void queueNumber(uint32_t n) {
  if (n == 0) {
    queueWord(spZERO);
  } else {
    if (n >= 1000) {
      uint32_t thousands = n / 1000;
      queueNumber(thousands);
      queueWord(spTHOUSAND);
      n %= 1000;
      if ((n > 0) && (n < 100)) queueWord(spAND);
    }
    if (n >= 100) {
      uint8_t hundreds = n / 100;
      queueNumber(hundreds);
      queueWord(spHUNDRED);
      n %= 100;
      if (n > 0) queueWord(spAND);
    }
    if (n > 19) {
      uint8_t tens = n / 10;
      switch (tens) {
        case 2: queueWord(spTWENTY); break;
        case 3: queueWord(spTHIR_); queueWord(spT); break;
        case 4: queueWord(spFOUR); queueWord(spT);  break;
        case 5: queueWord(spFIF_);  queueWord(spT); break;
        case 6: queueWord(spSIX);  queueWord(spT); break;
        case 7: queueWord(spSEVEN);  queueWord(spT); break;
        case 8: queueWord(spEIGHT);  queueWord(spT); break;
        case 9: queueWord(spNINE);  queueWord(spT); break;
      }
      n %= 10;
    }
    switch (n) {
      case 1: queueWord(spONE); break;
      case 2: queueWord(spTWO); break;
      case 3: queueWord(spTHREE); break;
      case 4: queueWord(spFOUR); break;
      case 5: queueWord(spFIVE); break;
      case 6: queueWord(spSIX); break;
      case 7: queueWord(spSEVEN); break;
      case 8: queueWord(spEIGHT); break;
      case 9: queueWord(spNINE); break;
      case 10: queueWord(spTEN); break;
      case 11: queueWord(spELEVEN); break;
      case 12: queueWord(spTWELVE); break;
      case 13: queueWord(spTHIR_); queueWord(sp_TEEN); break;
      case 14: queueWord(spFOUR); queueWord(sp_TEEN); break;
      case 15: queueWord(spFIF_); queueWord(sp_TEEN); break;
      case 16: queueWord(spSIX); queueWord(sp_TEEN); break;
      case 17: queueWord(spSEVEN); queueWord(sp_TEEN); break;
      case 18: queueWord(spEIGHT); queueWord(sp_TEEN); break;
      case 19: queueWord(spNINE); queueWord(sp_TEEN); break;
    }
  }
}

void queueReading(float grams) {
  SpokenReading reading;
  startPhrase();
  if (!splitReading(grams, reading)) {
    queueWord(spERROR);
    return;
  }
  if (reading.negative) queueWord(spMINUS);
  queueNumber(reading.whole);
  queueWord(spPOINT);
  queueNumber(reading.tenths);
  queueNumber(reading.hundredths);
  spokenValue = static_cast<int32_t>(reading.whole * 100 + reading.tenths * 10 + reading.hundredths);
  if (reading.negative) spokenValue = -spokenValue;
  speakingMeasurement = true;
}

bool validCalibration(float factor) {
  return validCalibrationFactor(factor);
}

void clearAcquisition() {
  readingFilter.reset();
  readingStable = stableWindow = false;
  calibrationSampler.reset();
  sampleCount = 0;
  readingAvailable = false;
}

void resetSamples() {
  recoveryNoticePending = false;
  announcements.reset();
  clearAcquisition();
}

void announceReady() {
  recoveryNoticePending = false;
  startPhrase();
  if (sensorFault) {
    queueWord(spERROR);
  } else if (calibrated) {
    queueWord(spREADY);
  } else {
    queueWord(spERROR);
    queueWord(spCALIBRATE);
  }
}

void beginOperation(Operation next) {
  startPhrase();
  resetSamples();
  operation = next;
  operationStartedAt = millis();
  zeroTimeoutReported = false;
  if (next == CALIBRATION_ZERO) {
    calibrationStatus = CalibrationStatus::Baseline;
    calibrationTimedOut = false;
    calibrationBaseline = SampleSummary();
    calibrationLoaded = SampleSummary();
  }
}

void calibrationFailed() {
  // The old offset and factor are untouched until a calibration succeeds.
  operation = WEIGHING;
  resetSamples();
  menu = true;
  menuOption = CALIBRATE;
  startPhrase();
  queueWord(spCALIBRATE);
  queueWord(spFAIL);
}

void finishAverage(long average) {
  switch (operation) {
    case STARTUP_ZERO:
      scale.set_offset(average);
      operation = WEIGHING;
      if (!calibrated) {
        menu = true;
        menuOption = CALIBRATE;
      }
      announceReady();
      break;
    case USER_ZERO:
      scale.set_offset(average);
      operation = WEIGHING;
      startPhrase();
      queueWord(spZERO);
      break;
    case CALIBRATION_ZERO:
      calibrationBaseline = calibrationSampler.summary();
      if (!baselineReady(calibrationBaseline)) {
        calibrationStatus = CalibrationStatus::BaselineMoving;
        break;
      }
      calibrationZero = calibrationBaseline.mean;
      operation = CALIBRATION_PROMPT;
      restartCalibrationPrompt = false;
      startPhrase();
      queueWord(spPUT);
      queueNumber(static_cast<uint32_t>(CALIBRATION_WEIGHT_GRAMS));
      break;
    case CALIBRATION_READ: {
      calibrationLoaded = calibrationSampler.summary();
      const CalibrationDecision decision = evaluateCalibration(
          calibrationBaseline, calibrationLoaded, CALIBRATION_WEIGHT_GRAMS,
          candidateCalibration);
      switch (decision) {
        case CalibrationDecision::Valid:
          operation = CALIBRATION_COMMIT;
          break;
        case CalibrationDecision::NoSignal:
          calibrationStatus = CalibrationStatus::NoSignal;
          break;
        case CalibrationDecision::BaselineMoving:
          calibrationStatus = CalibrationStatus::BaselineMoving;
          // The empty-platform measurement cannot improve while loaded.
          calibrationFailed();
          break;
        case CalibrationDecision::WeightMoving:
          calibrationStatus = CalibrationStatus::WeightMoving;
          break;
        case CalibrationDecision::InvalidFactor:
          calibrationStatus = CalibrationStatus::InvalidFactor;
          break;
      }
      break;
    }
    case WEIGHING:
      latestReading = (average - scale.get_offset()) / scale.get_scale();
      readingAvailable = true;
      break;
    default:
      break;
  }
}

void serviceOperation(unsigned long now) {
  if ((operation == STARTUP_ZERO || operation == USER_ZERO) &&
      now - operationStartedAt >= ZERO_TIMEOUT_MS && !zeroTimeoutReported) {
    zeroTimeoutReported = true;
    if (operation == USER_ZERO) {
      operation = WEIGHING;
      resetSamples();
    }
    startPhrase();
    queueWord(spERROR);
  }

  if ((operation == CALIBRATION_ZERO || operation == CALIBRATION_READ) &&
      now - operationStartedAt >= CALIBRATION_TIMEOUT_MS) {
    calibrationTimedOut = true;
    calibrationFailed();
    return;
  }
  if (operation == CALIBRATION_COMMIT && !button.busy() && !speechPaused) {
    if (!calibrationStore.save(candidateCalibration)) {
      calibrationStatus = CalibrationStatus::WriteFailed;
      calibrationFailed();
      return;
    }
    scale.set_offset(calibrationZero);
    scale.set_scale(candidateCalibration);
    calibrated = true;
    calibrationStatus = CalibrationStatus::Saved;
    menu = false;
    operation = WEIGHING;
    resetSamples();
    startPhrase();
    queueWord(spWEIGHT);
    queueNumber(static_cast<uint32_t>(CALIBRATION_WEIGHT_GRAMS));
  }

  if (operation == CALIBRATION_PROMPT) {
    if (speechPaused || button.busy()) return;
    if (restartCalibrationPrompt) {
      startPhrase();
      queueWord(spPUT);
      queueNumber(static_cast<uint32_t>(CALIBRATION_WEIGHT_GRAMS));
      restartCalibrationPrompt = false;
    } else if (speechIdle()) {
      // Only a complete "put five" prompt starts the placement interval.
      placementStartedAt = now;
      operation = CALIBRATION_WAIT;
    }
  } else if (operation == CALIBRATION_WAIT &&
             now - placementStartedAt >= WEIGHT_PLACEMENT_MS) {
    resetSamples();
    operation = CALIBRATION_READ;
    calibrationStatus = CalibrationStatus::Weight;
    operationStartedAt = now;
  }
}

void reportSensorFault() {
  if (sensorFault) {
    // A changed fault cause is visible in diagnostics; do not cut off the
    // notification already playing for the same fault episode.
    clearAcquisition();
    return;
  }
  const bool interruptedCalibration = operation >= CALIBRATION_ZERO;
  if (interruptedCalibration) calibrationStatus = CalibrationStatus::SensorFailed;
  sensorFault = true;
  recoveryNoticePending = false;
  resetSamples();
  if (operation != STARTUP_ZERO) operation = WEIGHING;
  startPhrase();
  if (interruptedCalibration) {
    queueWord(spCALIBRATE);
    queueWord(spFAIL);
  } else queueWord(spERROR);
}

void serviceScale(unsigned long now) {
  if (!scale.is_ready()) {
    if (!sensorFault && now - lastSampleAt >= SENSOR_TIMEOUT_MS) {
      sensorHealth.timeout();
      reportSensorFault();
    }
    return;
  }

  // bogde HX711 protects the clock pulses from Talkie's ISR on AVR.
  // Read just one ready conversion; never wait for a 20-conversion batch.
  const long raw = scale.read();
  latestRaw = raw;
  lastSampleAt = now;
  const SensorSample health = sensorHealth.add(static_cast<int32_t>(raw));
  if (health == SensorSample::Fault) {
    reportSensorFault();
    return;
  }
  if (health == SensorSample::Reject) {
    clearAcquisition();
    announcements.resetHistory();
    if (speakingMeasurement) startPhrase();
    return;
  }
  if (health == SensorSample::Recovered) {
    sensorFault = false;
    clearAcquisition();
    announcements.resetHistory();
    recoveryNoticePending = operation == WEIGHING && !menu;
  }

  // Drain conversions during prompts/placement so calibration starts with fresh data.
  if (operation == CALIBRATION_PROMPT || operation == CALIBRATION_WAIT ||
      operation == CALIBRATION_COMMIT ||
      (operation == WEIGHING && (menu || !calibrated))) return;

  if (operation == STARTUP_ZERO || operation == USER_ZERO || operation == WEIGHING) {
    readingFilter.add(static_cast<int32_t>(raw), static_cast<uint32_t>(now));
    readingStable = readingFilter.stable(scale.get_scale(), calibrated);
    if (!readingStable) stableWindow = false;
    else if (!stableWindow) {
      stableWindow = true;
      stableSince = now;
    }
    if (!readingFilter.ready()) return;
    if (operation != WEIGHING &&
        (!readingStable || now - stableSince < ZERO_SETTLE_MS)) return;
    finishAverage(readingFilter.mean());
    return;
  }

  if (calibrationSampler.add(static_cast<int32_t>(raw))) {
    finishAverage(calibrationSampler.summary().mean);
  }
  sampleCount = calibrationSampler.count();
}


void queueMode() {
  switch (announcements.mode()) {
    case AnnouncementMode::Repeat: queueWord(spREPEAT); break;
    case AnnouncementMode::Change: queueWord(spCHANGE); break;
    case AnnouncementMode::Manual: queueWord(spMANUAL); break;
  }
}

void loadMode() {
  const uint8_t mode = EEPROM.read(6);
  if (EEPROM.read(4) == 0xa6 && EEPROM.read(5) == 1 &&
      EEPROM.read(7) == static_cast<uint8_t>(~mode) && mode < 3) {
    announcements.setMode(static_cast<AnnouncementMode>(mode));
  }
}

void saveMode() {
  const uint8_t mode = static_cast<uint8_t>(announcements.mode());
  EEPROM.update(4, 0);
  EEPROM.update(5, 1);
  EEPROM.update(6, mode);
  EEPROM.update(7, static_cast<uint8_t>(~mode));
  EEPROM.update(4, 0xa6);
}

bool shouldAnnounce(unsigned long now) {
  SpokenReading reading;
  if (!splitReading(latestReading, reading)) return true; // Report invalid data audibly.
  int32_t value = static_cast<int32_t>(reading.whole * 100 + reading.tenths * 10 + reading.hundredths);
  if (reading.negative) value = -value;
  const bool settled = readingStable && now - stableSince >= ZERO_SETTLE_MS;
  return announcements.shouldSpeak(value, settled, static_cast<uint32_t>(now));
}

void buttonClick() {
  if (operation != WEIGHING) return;
  if (!menu) {
    announcements.request();
    return;
  }
  menuOption = menuOption == NO_SELECTION || menuOption == ANNOUNCEMENTS
                 ? ZERO_WEIGHT : static_cast<MenuOption>(menuOption + 1);
  startPhrase();
  if (menuOption == ZERO_WEIGHT) {
    queueWord(spZERO);
    queueWord(spWEIGHT);
  } else if (menuOption == CALIBRATE) {
    queueWord(spCALIBRATE);
  } else {
    queueWord(spCONTROL);
    queueMode();
  }
}

void buttonDoubleClick() {
  if (!menu || operation != WEIGHING) return;
  if (menuOption == ANNOUNCEMENTS) {
    const uint8_t next = (static_cast<uint8_t>(announcements.mode()) + 1) % 3;
    announcements.setMode(static_cast<AnnouncementMode>(next));
    saveMode();
    startPhrase();
    queueMode();
    return;
  }
  if (sensorFault) {
    startPhrase();
    queueWord(spERROR);
    return;
  }
  if (menuOption == ZERO_WEIGHT) {
    beginOperation(USER_ZERO);
  } else if (menuOption == CALIBRATE) {
    // The platform must be empty when this is selected.
    beginOperation(CALIBRATION_ZERO);
  }
}

void buttonPress() {
  if (operation == STARTUP_ZERO) return;
  if (operation != WEIGHING) {
    // Long press cancels a pending tare/calibration and preserves the saved factor.
    if (operation >= CALIBRATION_ZERO) calibrationStatus = CalibrationStatus::Cancelled;
    operation = WEIGHING;
    menu = false;
    resetSamples();
    announceReady();
    return;
  }
  menu = !menu;
  resetSamples();
  if (menu) {
    menuOption = NO_SELECTION;
    startPhrase();
    queueWord(spSET);
  } else {
    announceReady();
  }
}

void serviceButton(unsigned long now) {
  const ButtonEvent event = button.update(digitalRead(BUTTON_PIN) == LOW, now);
  if (event == ButtonEvent::Down) {
    if (operation == CALIBRATION_PROMPT) restartCalibrationPrompt = true;
    startPhrase();
    speechPaused = true;
    readingAvailable = false;
  } else if (event != ButtonEvent::None) {
    // A recognised hold may speak its menu feedback before the button is released.
    speechPaused = false;
    if (event == ButtonEvent::Click) buttonClick();
    else if (event == ButtonEvent::DoubleClick) buttonDoubleClick();
    else if (event == ButtonEvent::LongPress) buttonPress();
  }
}

void queueDiagnostics(unsigned long now) {
  SpokenReading diagnosticReading;
  const bool valid = calibrated && !sensorFault && readingFilter.ready() &&
                     operation == WEIGHING && !menu && splitReading(latestReading, diagnosticReading);
  console.beginMessage();
  console.print(F("raw=")); console.print(latestRaw);
  console.print(F(" mean=")); console.print(readingFilter.mean());
  console.print(F(" g="));
  if (valid) console.print(latestReading, 4); else console.print(F("na"));
  console.print(F(" spread=")); console.print(readingFilter.spread());
  console.print(F(" motion=")); console.print(readingFilter.meanSpread());
  console.print(F(" stable=")); console.print(readingStable ? 1 : 0);
  console.print(F(" zero=")); console.print(scale.get_offset());
  console.print(F(" net=")); console.print(readingFilter.mean() - scale.get_offset());
  console.print(F(" factor=")); console.print(scale.get_scale(), 4);
  console.print(F(" valid=")); console.print(valid ? 1 : 0);
  console.print(F(" op=")); console.print(static_cast<uint8_t>(operation));
  console.print(F(" fault=")); console.print(sensorFault ? (sensorHealth.clipped() ? 2 : 1) : 0);
  console.print(F(" mode=")); console.print(static_cast<uint8_t>(announcements.mode()));
  console.print(F(" age=")); console.println(now - lastSampleAt);
}

void printCalibrationStatus() {
  switch (calibrationStatus) {
    case CalibrationStatus::Idle: console.print(F("idle")); break;
    case CalibrationStatus::Baseline: console.print(F("baseline")); break;
    case CalibrationStatus::Weight: console.print(F("weight")); break;
    case CalibrationStatus::NoSignal: console.print(F("no-signal")); break;
    case CalibrationStatus::BaselineMoving: console.print(F("baseline-moving")); break;
    case CalibrationStatus::WeightMoving: console.print(F("weight-moving")); break;
    case CalibrationStatus::InvalidFactor: console.print(F("invalid-factor")); break;
    case CalibrationStatus::Saved: console.print(F("saved")); break;
    case CalibrationStatus::WriteFailed: console.print(F("write-failed")); break;
    case CalibrationStatus::SensorFailed: console.print(F("sensor-failed")); break;
    case CalibrationStatus::Cancelled: console.print(F("cancelled")); break;
  }
}

void queueCalibrationDiagnostics() {
  console.beginMessage();
  console.print(F("cal=")); printCalibrationStatus();
  console.print(F(" timedout=")); console.print(calibrationTimedOut ? 1 : 0);
  console.print(F(" op=")); console.print(static_cast<uint8_t>(operation));
  console.print(F(" raw=")); console.print(latestRaw);
  console.print(F(" n=")); console.print(sampleCount);
  console.print(F(" base=")); console.print(calibrationBaseline.mean);
  console.print(F(" bs=")); console.print(calibrationBaseline.spread);
  console.print(F(" bm=")); console.print(calibrationBaseline.movement);
  console.print(F(" load=")); console.print(calibrationLoaded.mean);
  console.print(F(" ls=")); console.print(calibrationLoaded.spread);
  console.print(F(" lm=")); console.print(calibrationLoaded.movement);
  console.print(F(" candidate="));
  console.println((calibrationLoaded.mean - calibrationBaseline.mean) / CALIBRATION_WEIGHT_GRAMS, 4);
}

void serviceConsole(unsigned long now) {
  // Process no more than 16 received bytes per loop, even under continuous input.
  for (uint8_t budget = 0; budget < 16 && Serial.available() > 0; ++budget) {
    const ConsoleCommand command = console.feed(static_cast<char>(Serial.read()));
    if (command != ConsoleCommand::None) {
      if (command == ConsoleCommand::StreamOn) diagnosticStream = true;
      if (command == ConsoleCommand::StreamOff) diagnosticStream = false;
      pendingCommand = command; // Coalesce requests if the output port is busy.
    }
  }
  console.drain(Serial);
  if (console.pending()) return;
  if (pendingCommand != ConsoleCommand::None) {
    if (pendingCommand == ConsoleCommand::Help) {
      console.beginMessage();
      console.println(F("help | status | raw | calibration | stream on | stream off; 9600 baud; snapshots at most 5 Hz"));
    } else if (pendingCommand == ConsoleCommand::Calibration) {
      queueCalibrationDiagnostics();
    } else if (pendingCommand == ConsoleCommand::Invalid) {
      console.beginMessage();
      console.println(F("error: unknown or oversized command; use help"));
    } else {
      queueDiagnostics(now);
    }
    pendingCommand = ConsoleCommand::None;
    lastDiagnosticAt = now;
  } else if (diagnosticStream && now - lastDiagnosticAt >= 200) {
    const bool showCalibration = operation >= CALIBRATION_ZERO ||
        (menu && menuOption == CALIBRATE && calibrationStatus != CalibrationStatus::Idle);
    if (showCalibration && streamCalibrationNext) queueCalibrationDiagnostics();
    else queueDiagnostics(now);
    streamCalibrationNext = showCalibration && !streamCalibrationNext;
    lastDiagnosticAt = now;
  }
}

void setup() {
  Serial.begin(9600);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  loadMode();

  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
  float factor;
  calibrated = calibrationStore.load(factor);
  scale.set_scale(calibrated ? factor : 1.0f);
  lastSampleAt = millis();
  operationStartedAt = lastSampleAt;
  // Tare the empty platform across loop iterations while still polling the button.
}

void loop() {
  serviceButton(millis());
  serviceSpeech();
  unsigned long now = millis();
  serviceOperation(now);
  now = millis(); // Calibration commits can take several EEPROM write cycles.
  serviceScale(now);
  if (recoveryNoticePending && !menu && operation == WEIGHING && speechIdle()) {
    recoveryNoticePending = false;
    announceReady();
  }
  serviceConsole(now);
  if (operation == WEIGHING && !menu && calibrated && !sensorFault &&
      !button.busy() && !speechPaused && readingAvailable && speechIdle() &&
      shouldAnnounce(now)) {
    queueReading(latestReading);
    readingAvailable = false;
  }
}
