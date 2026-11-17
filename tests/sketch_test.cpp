#include "Arduino.h"
#include "EEPROM.h"

#include <cassert>
#include <cstdio>
#include <cstring>

unsigned long testMillis = 0;
int testButtonLevel = HIGH;
bool testInterruptsEnabled = true;
bool NanoSpeechOutput::frozen = false;
bool NanoSpeechOutput::connected = false;
uint8_t NanoSpeechOutput::duty = 0;
uint8_t configuredButtonPin = 255;
uint8_t configuredButtonMode = 255;
TestSerial Serial;
TestEEPROM EEPROM;
bool serialTxBlocked = false;

unsigned long millis() { return testMillis; }
void pinMode(uint8_t pin, uint8_t mode) {
  configuredButtonPin = pin;
  configuredButtonMode = mode;
}
int digitalRead(uint8_t pin) {
  assert(pin == 2);
  return testButtonLevel;
}

// Exercise the firmware itself rather than a second copy of its state machine.
#include "../Talking_Scale/Talking_Scale.ino"

bool testVocabularyWord(const uint8_t *word) {
  const uint8_t *const vocabulary[] = {spPOINT, spSTART, spREADY, spERROR, spMINUS, spPUT, spCALIBRATE, spWEIGHT, spSET, spZERO, spONE, spTWO, spTHREE, spFOUR, spFIVE, spSIX, spSEVEN, spEIGHT, spNINE, spTEN, spELEVEN, spTWELVE, spTHIR_, spFIF_, sp_TEEN, spTWENTY, spT, spHUNDRED, spTHOUSAND, spAND, spCONTROL, spREPEAT, spCHANGE, spMANUAL, spFAIL};
  for (const uint8_t *known : vocabulary) if (word == known) return true;
  return false;
}

void advance(unsigned long duration) {
  for (unsigned long i = 0; i < duration; ++i) {
    ++testMillis;
    if (!serialTxBlocked) Serial.advance();
    loop();
  }
}

template <typename Condition>
void until(Condition complete, unsigned long timeout = 30000) {
  const unsigned long start = millis();
  while (!complete() && millis() - start < timeout) advance(1);
  assert(complete());
}

void click() {
  testButtonLevel = LOW;
  advance(80);
  testButtonLevel = HIGH;
  advance(700);
}

void doubleClick() {
  testButtonLevel = LOW;
  advance(80);
  testButtonLevel = HIGH;
  advance(120);
  testButtonLevel = LOW;
  advance(80);
  testButtonLevel = HIGH;
  advance(120);
}

void longPress() {
  testButtonLevel = LOW;
  advance(1150);
  testButtonLevel = HIGH;
  advance(100);
}

void boot(float factor = 1000.0f) {
  EEPROM.seed(factor);
  setup();
  assert(Serial.baud == 9600);
  assert(configuredButtonPin == 2 && configuredButtonMode == INPUT_PULLUP);
  assert(scale.dataPin == 4 && scale.clockPin == 5);
  assert(!voice.invertedOutput);
  until([] { return operation == WEIGHING; });
  assert(scale.get_offset() == 1000);
}

void enterMenu() {
  longPress();
  assert(menu);
  assert(menuOption == NO_SELECTION);
}

void beginCalibration() {
  enterMenu();
  click();
  assert(menuOption == ZERO_WEIGHT);
  click();
  assert(menuOption == CALIBRATE);
  scale.raw = 1500; // A new empty-platform offset, committed only on success.
  doubleClick();
  assert(operation == CALIBRATION_ZERO);
}

void waitForPlacement() {
  until([] { return operation == CALIBRATION_WAIT; });
}

void assertOldCalibration() {
  assert(scale.get_offset() == 1000);
  assert(scale.get_scale() == 1000.0f);
  float stored;
  assert(calibrationStore.load(stored));
  assert(stored == 1000.0f);
  assert(EEPROM.putCalls == 0);
}

void testSpeech() {
  queueReading(1.27f);
  assert(speechLength == 4);
  assert(speechWords[0] == spONE && speechWords[1] == spPOINT);
  assert(speechWords[2] == spTWO && speechWords[3] == spSEVEN);
  until([] { return speechIdle(); });
  assert(voice.words.size() == 4);
  for (size_t i = 1; i < voice.words.size(); ++i) {
    assert(voice.words[i].at >= voice.words[i - 1].at + voice.wordDurationMs);
  }
  queueReading(-0.03f);
  assert(speechLength == 5);
  assert(speechWords[0] == spMINUS && speechWords[1] == spZERO);
  assert(speechWords[2] == spPOINT && speechWords[3] == spZERO);
  assert(speechWords[4] == spTHREE);
}

void testButtonAndMenu() {
  boot();
  voice.wordDurationMs = 5000;
  queueReading(1.27f);
  advance(600);
  assert(voice.isTalking());
  assert(readingFilter.count() > 0);
  enterMenu(); // A long press while speech and averaging run.
  assert(voice.isTalking());
  assert(readingFilter.count() == 0);
  click();
  assert(menuOption == ZERO_WEIGHT);
  click();
  assert(menuOption == CALIBRATE);
  click();
  assert(menuOption == ANNOUNCEMENTS);
  click(); // Wrap from speech settings back to Zero Weight.
  assert(menuOption == ZERO_WEIGHT);
  scale.raw = 2000;
  doubleClick();
  assert(operation == USER_ZERO);
  assert(scale.get_offset() == 1000);
  until([] { return operation == WEIGHING; });
  assert(scale.get_offset() == 2000);
  assert(EEPROM.putCalls == 0);
  longPress();
  assert(!menu);
}

void testCalibration(bool negative) {
  boot();
  beginCalibration();
  voice.wordDurationMs = 1200;
  until([] { return operation == CALIBRATION_PROMPT; });
  const unsigned long promptStarted = millis();
  assertOldCalibration();
  waitForPlacement();
  assert(millis() - promptStarted >= 2400);
  assert(voice.words[voice.words.size() - 2].data == spPUT);
  assert(voice.words.back().data == spFIVE);
  assert(placementStartedAt >= voice.words.back().at + voice.wordDurationMs);
  assert(placementStartedAt == millis());
  scale.raw = negative ? -8500 : 11500;
  advance(WEIGHT_PLACEMENT_MS - 1);
  assert(operation == CALIBRATION_WAIT);
  advance(1);
  assert(operation == CALIBRATION_READ && sampleCount <= 1);
  until([] { return operation == WEIGHING; });
  assert(calibrated && !menu);
  assert(scale.get_offset() == 1500);
  const float expectedFactor = negative ? -2000.0f : 2000.0f;
  assert(scale.get_scale() == expectedFactor);
  float stored;
  assert(calibrationStore.load(stored));
  assert(stored == expectedFactor);
  assert(EEPROM.putCalls == 0);
  assert(EEPROM.changedBytes > 0);
  // The journal preserves the legacy factor and reserved user-settings bytes.
  for (size_t i = sizeof(float); i < 8; ++i) {
    assert(EEPROM.bytes[i] == 0xff);
  }
}

void testCancel(const char *stage) {
  boot();
  beginCalibration();
  if (std::strcmp(stage, "zero") == 0) {
    scale.connected = false; // Keep the zero collection pending during the hold.
  } else if (std::strcmp(stage, "prompt") == 0) {
    voice.wordDurationMs = 2000;
    until([] { return operation == CALIBRATION_PROMPT; });
  } else {
    waitForPlacement();
    if (std::strcmp(stage, "read") == 0) {
      advance(WEIGHT_PLACEMENT_MS);
      assert(operation == CALIBRATION_READ);
      scale.raw = 11500;
    }
  }
  longPress();
  assert(operation == WEIGHING && !menu);
  assertOldCalibration();
}

void testCalibrationFailure(bool timeout) {
  boot();
  beginCalibration();
  waitForPlacement();
  advance(WEIGHT_PLACEMENT_MS);
  assert(operation == CALIBRATION_READ);
  if (timeout) {
    scale.connected = false;
    advance(SENSOR_TIMEOUT_MS + 1);
    assert(sensorFault);
  } else {
    // Leaving the empty platform unchanged gives an invalid zero factor.
    until([] { return operation == WEIGHING; });
    assert(!sensorFault);
  }
  assert(operation == WEIGHING && menu);
  assert(menuOption == CALIBRATE);
  assertOldCalibration();
}

void testInvalidEEPROM(const char *scenario) {
  if (std::strcmp(scenario, "zero-factor") == 0) EEPROM.seed(0.0f);
  if (std::strcmp(scenario, "nan-factor") == 0) EEPROM.seed(NAN);
  if (std::strcmp(scenario, "infinity-factor") == 0) EEPROM.seed(INFINITY);
  const auto originalBytes = EEPROM.bytes;
  setup();
  assert(!calibrated && scale.get_scale() == 1.0f);
  until([] { return operation == WEIGHING; });
  assert(menu && menuOption == CALIBRATE);
  advance(2500);
  assert(voice.words.size() == 2);
  assert(voice.words[0].data == spERROR);
  assert(voice.words[1].data == spCALIBRATE);
  assert(EEPROM.bytes == originalBytes && EEPROM.putCalls == 0);
  assert(!readingAvailable);
  doubleClick();
  assert(operation == CALIBRATION_ZERO); // Calibration is immediately selectable.
}

void testStartupTimeout() {
  EEPROM.seed(1000.0f);
  scale.connected = false;
  setup();
  advance(SENSOR_TIMEOUT_MS + 1);
  assert(sensorFault && operation == STARTUP_ZERO);
  assert(scale.readCount == 0);
  scale.connected = true;
  until([] { return operation == WEIGHING; });
  assert(!sensorFault && scale.get_offset() == 1000);
  assert(EEPROM.putCalls == 0);
}

void testNoisyStartup() {
  EEPROM.seed(2000.0f);
  setup();
  const auto storedBytes = EEPROM.bytes;
  for (unsigned int i = 0; operation == STARTUP_ZERO && i < 60; ++i) {
    scale.raw = 1000 + ((i % 2) ? 20 : -20);
    advance(100);
  }
  // Forty counts of ADC noise exceed the former 0.01 g peak-range gate.
  // The short averaged blocks repeat reliably and permit the empty-platform tare.
  assert(operation == WEIGHING && !sensorFault && !zeroTimeoutReported);
  assert(readingFilter.spread() == 40);
  assert(readingFilter.meanSpread() <= 20);
  assert(scale.get_offset() == 1000 && scale.get_scale() == 2000.0f);
  assert(EEPROM.putCalls == 0 && EEPROM.bytes == storedBytes);
}

void testStartupWithUnrealisticFactor(float factor) {
  assert(calibrationStore.save(factor));
  const auto storedBytes = EEPROM.bytes;
  const unsigned int writes = EEPROM.changedBytes;
  setup();
  for (unsigned int i = 0; operation == STARTUP_ZERO && i < 100; ++i) {
    scale.raw = 1000 + ((i % 2) ? 20 : -20);
    advance(100);
  }
  // A saved factor cannot change whether the same empty-platform samples settle.
  assert(operation == WEIGHING && !sensorFault && !zeroTimeoutReported);
  assert(calibrated && scale.get_offset() == 1000 && scale.get_scale() == factor);
  assert(EEPROM.bytes == storedBytes && EEPROM.changedBytes == writes);
  enterMenu();
  assert(operation == WEIGHING && calibrated);
  assert(EEPROM.bytes == storedBytes && EEPROM.changedBytes == writes);
}

void testMovingStartupWithLargeFactor() {
  assert(calibrationStore.save(1.0e20f));
  const auto storedBytes = EEPROM.bytes;
  setup();
  for (unsigned int i = 0; i < 200; ++i) {
    scale.raw = 1000 + static_cast<long>(i) * 4;
    advance(100);
    // This drift is well below the old uncalibrated 4096-count allowance, but
    // zero must not be captured while the platform continues moving.
    assert(operation == STARTUP_ZERO && scale.get_offset() == 0);
    assert(!readingAvailable && !sensorFault);
  }
  assert(zeroTimeoutReported && EEPROM.bytes == storedBytes);
  scale.raw = 2000;
  until([] { return operation == WEIGHING; });
  assert(calibrated && scale.get_offset() == 2000);
  assert(scale.get_scale() == 1.0e20f && EEPROM.bytes == storedBytes);
}

void testStartupFaultWithUnrealisticFactor(bool clipped) {
  assert(calibrationStore.save(1.0e20f));
  const auto storedBytes = EEPROM.bytes;
  scale.raw = clipped ? 8388607L : 1000;
  scale.connected = clipped;
  setup();
  advance(SENSOR_TIMEOUT_MS + 1000);
  assert(operation == STARTUP_ZERO && sensorFault && scale.get_offset() == 0);
  assert(!readingAvailable && EEPROM.bytes == storedBytes);
  scale.connected = true;
  scale.raw = 1000;
  until([] { return operation == WEIGHING; });
  assert(!sensorFault && calibrated && scale.get_offset() == 1000);
  assert(scale.get_scale() == 1.0e20f && EEPROM.bytes == storedBytes);
}

void testRecalibrateAfterUnrealisticStartupFactor() {
  const float originalFactor = 1.0e-20f;
  assert(calibrationStore.save(originalFactor));
  setup();
  for (unsigned int i = 0; operation == STARTUP_ZERO && i < 100; ++i) {
    scale.raw = 1000 + ((i % 2) ? 20 : -20);
    advance(100);
  }
  assert(operation == WEIGHING && calibrated && scale.get_scale() == originalFactor);
  beginCalibration();
  waitForPlacement();
  scale.raw = 11500;
  until([] { return operation == WEIGHING; });
  assert(calibrated && !menu && calibrationStatus == CalibrationStatus::Saved);
  assert(scale.get_offset() == 1500 && scale.get_scale() == 2000.0f);
  float stored;
  assert(calibrationStore.load(stored) && stored == 2000.0f);
  assert(EEPROM.bytes[8] == 0xa5 && EEPROM.bytes[9] == 0xff); // No factory reset.
}

void testStandaloneOperation() {
  // Even a permanently full UART transmit buffer must not hold up the scale.
  serialTxBlocked = true;
  Serial.space = 0;
  Serial.send("status\n");
  boot();
  announcements.setMode(AnnouncementMode::Manual);
  assert(console.pending() && Serial.output.empty() && Serial.space == 0);

  beginCalibration();
  waitForPlacement();
  assert(console.pending() && Serial.output.empty() && Serial.space == 0);
  scale.raw = 11500;
  until([] { return operation == WEIGHING; });
  assert(calibrationStatus == CalibrationStatus::Saved && !menu);
  assert(scale.get_offset() == 1500 && scale.get_scale() == 2000.0f);
  float stored;
  assert(calibrationStore.load(stored) && stored == 2000.0f);

  until([] { return speechIdle() && readingAvailable; });
  assert(latestReading == 5.0f);
  const size_t wordCount = voice.words.size();
  click();
  until([=] { return voice.words.size() == wordCount + 4 && speechIdle(); });
  assert(voice.words[wordCount].data == spFIVE);
  assert(voice.words[wordCount + 1].data == spPOINT);
  assert(voice.words[wordCount + 2].data == spZERO);
  assert(voice.words[wordCount + 3].data == spZERO);
  assert(console.pending() && Serial.output.empty() && Serial.space == 0);
  assert(!sensorFault && operation == WEIGHING && testInterruptsEnabled);
}

void testSensorRecovery() {
  boot();
  scale.raw = 2000;
  advance(600);
  assert(readingFilter.count() > 0);
  scale.connected = false;
  advance(SENSOR_TIMEOUT_MS + 1);
  assert(sensorFault && readingFilter.count() == 0);
  assertOldCalibration();
  scale.raw = 3000;
  scale.connected = true;
  const unsigned int readCount = scale.readCount;
  const float beforeRecovery = latestReading;
  until([=] { return scale.readCount == readCount + ScaleFilter::WINDOW_SAMPLES + 5; });
  assert(!sensorFault && latestReading == beforeRecovery);
  until([=] { return scale.readCount == readCount + ScaleFilter::WINDOW_SAMPLES + 6; });
  assert(latestReading == 2.0f); // No stale samples from before disconnection.
}

void testFaultedMenuExit() {
  boot();
  enterMenu();
  scale.connected = false;
  advance(SENSOR_TIMEOUT_MS + 1);
  assert(sensorFault && menu);
  const size_t wordCount = voice.words.size();
  longPress();
  assert(sensorFault && !menu);
  assert(speechLength == 1 && speechWords[0] == spERROR);
  for (size_t i = wordCount; i < voice.words.size(); ++i) {
    assert(voice.words[i].data != spREADY);
  }
  assertOldCalibration();
}

void testSpeechInterruption() {
  boot();
  voice.wordDurationMs = 5000;
  queueReading(1.27f);
  const size_t beforeStart = voice.words.size();
  until([=] { return voice.words.size() > beforeStart; });
  assert(voice.isTalking());
  const size_t started = voice.words.size();
  const size_t stopped = voice.terminationTimes.size();
  const unsigned long pressedAt = millis();
  testButtonLevel = LOW;
  advance(35);
  assert(NanoSpeechOutput::frozen); // Synthesis stops at the debounced press.
  assert(millis() <= pressedAt + 35);
  assert(speechLength == 0 && speechPaused);
  assert(voice.terminationTimes.size() == stopped); // Ramp first, then release D3.
  advance(70);
  assert(!voice.isTalking());
  assert(voice.terminationTimes.size() == stopped + 1);
  assert(testInterruptsEnabled);
  advance(500);
  assert(voice.words.size() == started); // No old suffix or fresh measurement while held.
  testButtonLevel = HIGH;
  advance(550);
  assert(voice.words.size() == started); // Wait for the click/double-click decision.
  advance(100);
  assert(!speechPaused && !button.busy() && !menu);
  until([=] { return voice.words.size() > started; });
  assert(voice.words.back().data == spZERO); // A fresh zero reading, not old 1.27.

  // Immediate cancellation must also preserve an already-disabled interrupt state.
  testInterruptsEnabled = false;
  startPhrase();
  assert(!testInterruptsEnabled && NanoSpeechOutput::frozen);
  testInterruptsEnabled = true;
}

void testInterruptedCalibrationPrompt() {
  boot();
  beginCalibration();
  voice.wordDurationMs = 1200;
  until([] { return operation == CALIBRATION_PROMPT && !voice.words.empty() &&
                        voice.words.back().data == spPUT; });
  const size_t firstPrompt = voice.words.size() - 1;
  assert(voice.words.back().data == spPUT);
  click();
  assert(operation == CALIBRATION_PROMPT);
  assert(placementStartedAt == 0);
  waitForPlacement();
  assert(voice.words.size() == firstPrompt + 3);
  assert(voice.words[firstPrompt + 1].data == spPUT);
  assert(voice.words[firstPrompt + 2].data == spFIVE);
  assert(placementStartedAt >= voice.words.back().at + voice.wordDurationMs);
  assertOldCalibration();
}

void testPhraseBound() {
  for (uint32_t value = 0; value < 1000000; ++value) {
    startPhrase();
    queueNumber(value);
    assert(speechLength <= 13);
    // Sign, point, and the two decimal digits must fit as well.
    assert(speechLength + 4 <= MAX_PHRASE_WORDS);
  }
}

void testCalibrationRejectsNoise() {
  boot();
  beginCalibration();
  waitForPlacement();
  scale.raw = 1501; // One raw count of unloaded drift must not become a calibration.
  until([] { return operation == WEIGHING; });
  assert(menu && menuOption == CALIBRATE);
  assertOldCalibration();
}

void testLateCalibrationCancel() {
  boot();
  beginCalibration();
  waitForPlacement();
  scale.raw = 11500;
  until([] { return operation == CALIBRATION_READ && calibrationSampler.windowCount() >= 1 && sampleCount >= 18; });
  testButtonLevel = LOW;
  advance(1050);
  assert(operation == WEIGHING && !menu);
  assertOldCalibration();
  testButtonLevel = HIGH;
  advance(50);
}

void testUnstableTare() {
  boot();
  enterMenu();
  click();
  doubleClick();
  assert(operation == USER_ZERO);
  for (unsigned int i = 0; i < 155; ++i) {
    scale.raw = (i % 2) ? 2000 : 3000;
    advance(100);
  }
  assert(operation == WEIGHING && menu);
  assert(scale.get_offset() == 1000);
  assert(EEPROM.putCalls == 0);
}

void testAnnouncementModes() {
  boot();
  enterMenu();
  click(); click(); click();
  assert(menuOption == ANNOUNCEMENTS);
  doubleClick();
  assert(announcements.mode() == AnnouncementMode::Change);
  doubleClick();
  assert(announcements.mode() == AnnouncementMode::Manual);
  longPress();
  until([] { return speechIdle(); });
  const size_t wordCount = voice.words.size();
  advance(5000);
  assert(voice.words.size() == wordCount);
  click();
  until([] { return speakingMeasurement; });
  until([] { return !speakingMeasurement; });
  assert(voice.words.size() > wordCount);
  const size_t completed = voice.words.size();
  advance(4000);
  assert(voice.words.size() == completed);
  announcements.setMode(AnnouncementMode::Repeat);
  loadMode();
  assert(announcements.mode() == AnnouncementMode::Manual);
}

void testClippedSensor() {
  boot();
  announcements.setMode(AnnouncementMode::Manual);
  scale.raw = 8388607;
  advance(350);
  assert(sensorFault && sensorHealth.clipped());
  assert(!readingAvailable && readingFilter.count() == 0);
  assert(speechLength == 1 && speechWords[0] == spERROR);
  assertOldCalibration();
  scale.raw = 1000;
  advance(300);
  assert(sensorFault);
  advance(300);
  assert(!sensorFault);
  until([] { return readingFilter.ready(); });
  assert(latestReading == 0.0f);
}

void testConsole() {
  boot();
  announcements.setMode(AnnouncementMode::Manual);
  Serial.send("help\nstatus\n");
  advance(500);
  assert(Serial.output.find("raw=") != std::string::npos);
  assert(Serial.output.find("factor=1000") != std::string::npos);
  assert(Serial.output.find("mode=2") != std::string::npos);
  Serial.send("stream on\n");
  advance(500);
  assert(diagnosticStream);
  // A stopped output port must not block a hold, speech or sampling.
  Serial.space = 0;
  testButtonLevel = LOW;
  for (int i = 0; i < 1100; ++i) {
    ++testMillis;
    Serial.send("status\nstatus\nstatus\n");
    loop();
  }
  assert(menu);
  assert(Serial.space == 0);
  testButtonLevel = HIGH;
  Serial.input.clear();
  Serial.send("\nstream off\n");
  advance(500);
  assert(!diagnosticStream);
  assertOldCalibration();
  // A finite but unusably small legacy factor can overflow converted grams.
  scale.set_scale(1.0e-38f);
  scale.raw = 2000;
  menu = false;
  advance(3000);
  assert(!isfinite(latestReading));
  until([] { return !console.pending(); });
  Serial.output.clear();
  Serial.send("status\n");
  advance(500);
  assert(Serial.output.find("g=na") != std::string::npos);
  assert(Serial.output.find("valid=0") != std::string::npos);
}

void testRequestSurvivesClip() {
  boot();
  announcements.setMode(AnnouncementMode::Manual);
  announcements.request();
  scale.raw = 8388607;
  advance(100);
  assert(!sensorFault && !readingAvailable);
  scale.raw = 1000;
  until([] { return speakingMeasurement; });
}

void testRepeatGap(bool requestDuringGap) {
  boot();
  until([] { return speakingMeasurement; });
  until([] { return !speakingMeasurement; });
  const unsigned long finishedAt = millis();
  const size_t words = voice.words.size();
  if (requestDuringGap) {
    advance(100);
    click();
    until([=] { return voice.words.size() > words; });
    assert(millis() - finishedAt < 2000);
  } else {
    advance(1999);
    assert(voice.words.size() == words && !playback.busy());
    until([=] { return voice.words.size() > words; });
    assert(voice.words[words].at - finishedAt >= 2000);
  }
  assert(testInterruptsEnabled);
}

void testNoisyCalibration() {
  boot(10000.0f); // The previous calibration must not constrain a new one.
  beginCalibration();
  for (unsigned int i = 0; operation == CALIBRATION_ZERO && i < 150; ++i) {
    scale.raw = 1500 + ((i % 2) ? 20 : -20);
    advance(100);
  }
  assert(operation == CALIBRATION_PROMPT);
  assert(calibrationBaseline.spread == 40);
  waitForPlacement();
  advance(WEIGHT_PLACEMENT_MS);
  for (unsigned int i = 0; operation == CALIBRATION_READ && i < 150; ++i) {
    scale.raw = 11500 + ((i % 2) ? 20 : -20);
    advance(100);
  }
  until([] { return operation == WEIGHING; });
  assert(calibrationStatus == CalibrationStatus::Saved && !menu);
  assert(fabsf(scale.get_scale() - 2000.0f) <= 1.0f);
  assert(!calibrationTimedOut);
  float stored;
  assert(calibrationStore.load(stored));
  assert(stored == scale.get_scale());
}

void testCalibrationFailureDetails() {
  boot();
  beginCalibration();
  waitForPlacement();
  until([] { return operation == WEIGHING; }); // No reference weight placed.
  assert(calibrationStatus == CalibrationStatus::NoSignal && calibrationTimedOut);
  assert(menu && menuOption == CALIBRATE);
  until([] { return speechIdle(); });
  assert(voice.words.size() >= 2);
  assert(voice.words[voice.words.size() - 2].data == spCALIBRATE);
  assert(voice.words.back().data == spFAIL);
  Serial.output.clear();
  Serial.send("calibration\n");
  advance(500);
  assert(Serial.output.find("cal=no-signal") != std::string::npos);
  assert(Serial.output.find("timedout=1") != std::string::npos);
  assertOldCalibration();
}

void testCalibrationFaultSpeech() {
  boot();
  beginCalibration();
  waitForPlacement();
  advance(WEIGHT_PLACEMENT_MS);
  voice.wordDurationMs = 1200;
  scale.connected = false;
  until([] { return sensorFault; });
  assert(calibrationStatus == CalibrationStatus::SensorFailed && menu);
  scale.raw = 8388607;
  scale.connected = true;
  advance(350); // A changed fault cause must not replace the pending failure cue.
  assert(sensorFault && sensorHealth.clipped());
  scale.raw = 1000;
  until([] { return !sensorFault; });
  until([] { return speechIdle(); });
  assert(voice.words.size() >= 2);
  assert(voice.words[voice.words.size() - 2].data == spCALIBRATE);
  assert(voice.words.back().data == spFAIL);
  assertOldCalibration();
}

void assertErased() {
  for (uint8_t value : EEPROM.bytes) assert(value == 0xff);
}

void testStartupReset() {
  EEPROM.bytes.fill(0x35);
  EEPROM.seed(1.0e-20f); // A finite but unusably small saved calibration.
  announcements.setMode(AnnouncementMode::Manual);
  saveMode();
  const auto original = EEPROM.bytes;
  testButtonLevel = LOW;
  setup();
  assert(startupState == StartupState::ConfirmReset && !calibrated);
  advance(4999);
  assert(EEPROM.bytes == original && scale.readCount == 0 && !sensorFault);
  assert(voice.words.empty());
  advance(1);
  assert(startupState == StartupState::Erasing && eepromReset.pending());
  until([] { return startupState == StartupState::WaitRelease; });
  assertErased();
  assert(scale.readCount == 0 && !sensorFault && !menu);
  const unsigned int writes = EEPROM.changedBytes;
  advance(10000); // Continuing to hold cannot erase twice or open the menu.
  assert(startupState == StartupState::WaitRelease && EEPROM.changedBytes == writes);
  assert(!voice.words.empty() && voice.words.back().data == spZERO);
  testButtonLevel = HIGH;
  advance(30);
  assert(startupState == StartupState::WaitRelease);
  advance(1);
  assert(startupState == StartupState::Normal && scale.readCount == 0);
  until([] { return operation == WEIGHING; });
  assert(!calibrated && menu && menuOption == CALIBRATE);
  assert(announcements.mode() == AnnouncementMode::Repeat && scale.get_scale() == 1.0f);
  assertErased();

  // The cleared scale can calibrate directly from the selected menu item.
  doubleClick();
  assert(operation == CALIBRATION_ZERO);
  scale.raw = 1500;
  waitForPlacement();
  scale.raw = 11500;
  until([] { return operation == WEIGHING; });
  assert(calibrated && !menu && scale.get_scale() == 2000.0f);
}

void testCancelledStartupReset() {
  EEPROM.seed(7000.0f);
  assert(calibrationStore.save(2000.0f));
  announcements.setMode(AnnouncementMode::Change);
  saveMode();
  const auto original = EEPROM.bytes;
  testButtonLevel = LOW;
  setup();
  advance(4999);
  assert(EEPROM.bytes == original && scale.readCount == 0);
  testButtonLevel = HIGH;
  advance(1);
  assert(startupState == StartupState::Normal);
  until([] { return operation == WEIGHING; });
  assert(EEPROM.bytes == original && calibrated && scale.get_scale() == 2000.0f);
  assert(announcements.mode() == AnnouncementMode::Change && !menu);
}

void testResumedStartupReset() {
  EEPROM.bytes.fill(0x35);
  EEPROM.bytes[9] = 0x52; // A previously confirmed reset interrupted by power loss.
  setup();
  assert(startupState == StartupState::Erasing && !calibrated);
  until([] { return startupState == StartupState::WaitRelease; });
  assertErased();
  assert(scale.readCount == 0 && !sensorFault);
  until([] { return startupState == StartupState::Normal; });
  until([] { return operation == WEIGHING; });
  assert(!calibrated && menu && menuOption == CALIBRATE);
  assertErased();
}

void testFailedStartupReset(bool markerFailure) {
  EEPROM.bytes.fill(0x35);
  const auto original = EEPROM.bytes;
  EEPROM.failedAddress = markerFailure ? 9 : 100;
  testButtonLevel = LOW;
  setup();
  until([] { return startupState == StartupState::ResetFailed; });
  assert(!calibrated && scale.readCount == 0 && !sensorFault);
  if (markerFailure) assert(EEPROM.bytes == original && !eepromReset.pending());
  else assert(eepromReset.pending() && EEPROM.bytes[100] == 0x35);
  testButtonLevel = HIGH;
  until([] { return speechIdle(); });
  assert(voice.words.size() == 2 && voice.words[0].data == spERROR);
  assert(voice.words[1].data == spFAIL);
  const unsigned int writes = EEPROM.changedBytes;
  advance(5000);
  assert(startupState == StartupState::ResetFailed && scale.readCount == 0);
  assert(EEPROM.changedBytes == writes);
}

void testLateResetHold() {
  boot();
  const auto original = EEPROM.bytes;
  testButtonLevel = LOW;
  advance(10000);
  testButtonLevel = HIGH;
  advance(100);
  assert(startupState == StartupState::Normal && operation == WEIGHING && menu);
  assert(calibrated && EEPROM.bytes == original && !eepromReset.pending());
}

int main(int argc, char **argv) {
  assert(argc == 2);
  const char *scenario = argv[1];
  if (std::strcmp(scenario, "speech") == 0) testSpeech();
  else if (std::strcmp(scenario, "button-menu") == 0) testButtonAndMenu();
  else if (std::strcmp(scenario, "calibration") == 0) testCalibration(false);
  else if (std::strcmp(scenario, "calibration-negative") == 0) testCalibration(true);
  else if (std::strncmp(scenario, "cancel-", 7) == 0) testCancel(scenario + 7);
  else if (std::strcmp(scenario, "calibration-failure") == 0) testCalibrationFailure(false);
  else if (std::strcmp(scenario, "calibration-timeout") == 0) testCalibrationFailure(true);
  else if (std::strcmp(scenario, "startup-timeout") == 0) testStartupTimeout();
  else if (std::strcmp(scenario, "startup-jitter") == 0) testNoisyStartup();
  else if (std::strcmp(scenario, "startup-factor-small") == 0) testStartupWithUnrealisticFactor(1.0e-20f);
  else if (std::strcmp(scenario, "startup-factor-small-negative") == 0) testStartupWithUnrealisticFactor(-1.0e-20f);
  else if (std::strcmp(scenario, "startup-factor-large") == 0) testStartupWithUnrealisticFactor(1.0e20f);
  else if (std::strcmp(scenario, "startup-factor-large-negative") == 0) testStartupWithUnrealisticFactor(-1.0e20f);
  else if (std::strcmp(scenario, "startup-moving-large-factor") == 0) testMovingStartupWithLargeFactor();
  else if (std::strcmp(scenario, "startup-clipped-large-factor") == 0) testStartupFaultWithUnrealisticFactor(true);
  else if (std::strcmp(scenario, "startup-missing-large-factor") == 0) testStartupFaultWithUnrealisticFactor(false);
  else if (std::strcmp(scenario, "startup-recalibrate") == 0) testRecalibrateAfterUnrealisticStartupFactor();
  else if (std::strcmp(scenario, "standalone") == 0) testStandaloneOperation();
  else if (std::strcmp(scenario, "sensor-recovery") == 0) testSensorRecovery();
  else if (std::strcmp(scenario, "fault-menu-exit") == 0) testFaultedMenuExit();
  else if (std::strcmp(scenario, "interrupt") == 0) testSpeechInterruption();
  else if (std::strcmp(scenario, "interrupt-calibration") == 0) testInterruptedCalibrationPrompt();
  else if (std::strcmp(scenario, "calibration-noise") == 0) testCalibrationRejectsNoise();
  else if (std::strcmp(scenario, "late-cancel") == 0) testLateCalibrationCancel();
  else if (std::strcmp(scenario, "unstable-tare") == 0) testUnstableTare();
  else if (std::strcmp(scenario, "modes") == 0) testAnnouncementModes();
  else if (std::strcmp(scenario, "clipping") == 0) testClippedSensor();
  else if (std::strcmp(scenario, "console") == 0) testConsole();
  else if (std::strcmp(scenario, "clip-request") == 0) testRequestSurvivesClip();
  else if (std::strcmp(scenario, "repeat-gap") == 0) testRepeatGap(false);
  else if (std::strcmp(scenario, "gap-button") == 0) testRepeatGap(true);
  else if (std::strcmp(scenario, "calibration-jitter") == 0) testNoisyCalibration();
  else if (std::strcmp(scenario, "calibration-details") == 0) testCalibrationFailureDetails();
  else if (std::strcmp(scenario, "calibration-fault-speech") == 0) testCalibrationFaultSpeech();
  else if (std::strcmp(scenario, "reset-confirm") == 0) testStartupReset();
  else if (std::strcmp(scenario, "reset-cancel") == 0) testCancelledStartupReset();
  else if (std::strcmp(scenario, "reset-resume") == 0) testResumedStartupReset();
  else if (std::strcmp(scenario, "reset-failure") == 0) testFailedStartupReset(false);
  else if (std::strcmp(scenario, "reset-marker-failure") == 0) testFailedStartupReset(true);
  else if (std::strcmp(scenario, "reset-latepress") == 0) testLateResetHold();
  else if (std::strcmp(scenario, "phrase-bound") == 0) testPhraseBound();
  else testInvalidEEPROM(scenario);
  std::printf("Sketch test passed: %s\n", scenario);
}
