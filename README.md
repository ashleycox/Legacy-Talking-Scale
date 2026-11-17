# Legacy Talking Scale

A talking tracking force scale using a 16 MHz ATmega328P Arduino Nano, an HX711,
and Talkie speech synthesis. It was created to let a visually impaired person
set a turntable cartridge's tracking force. This version updates the software
for the original hardware and retains its speech samples.

The [original build instructions](https://theblindmansworkshop.com/build-a-talking-scale-with-an-arduino-hx711-and-the-talkie-library/)
describe the enclosure, load cell, power supply, and wiring.

## Hardware

The pin assignments are unchanged:

| Connection                                   | Arduino pin |
| -------------------------------------------- | ----------- |
| Button to ground, using the internal pull-up | D2          |
| Speaker positive; speaker negative to ground | D3          |
| HX711 DT / DOUT                              | D4          |
| HX711 SCK                                    | D5          |

Talkie's inverted D11 output is disabled. The sketch requires a 16 MHz
ATmega328P: Talkie's 8 MHz mode disables the clock used to time button gestures.
The same pin mapping also works on a 16 MHz ATmega328P Uno.

## Libraries and building

Use these tested releases from Arduino Library Manager:

| Library                                                 | Version | Maintainer          |
| ------------------------------------------------------- | ------- | ------------------- |
| [Talkie](https://github.com/ArminJo/Talkie)             | 1.4.0   | Armin Joachimsmeyer |
| [HX711 Arduino Library](https://github.com/bogde/HX711) | 0.7.5   | Bogdan Necula       |

Choose **HX711 Arduino Library**; the library named just **HX711** has a
different API. EEPROM support comes from the Arduino AVR core. EEPROMEx and
OneButton are no longer needed; the button is handled directly in
`ScaleButton.h`. The tested Arduino AVR Boards core is 1.8.8.

For the Arduino IDE, move the old bundled libraries out of the sketchbook's
`libraries` folder before installing the releases above. Open
`Talking_Scale/Talking_Scale.ino`, select Arduino Nano and the matching
ATmega328P processor/bootloader, then verify and upload. Many older Nano clones
need **ATmega328P (Old Bootloader)**. Keep the platform empty during startup.

Battery operation does not require a USB connection or an open serial monitor.
Startup waits for sensor conversions and a stable empty-platform average;
measurements and the menu become available after this initial zero. A broad
range of individual samples can still produce a repeatable average, so startup
checks movement between averaged blocks rather than the individual-sample range.
This startup check uses sensor counts and background noise directly, independent
of the saved calibration factor. An incorrect factor cannot lock out startup or
the menu; correcting inaccurate weights still requires the 5 g calibration.

For Arduino CLI, the included [build profiles](https://docs.arduino.cc/arduino-cli/sketch-project-file/)
download the specified versions into isolated storage, avoiding conflicts with
other installed libraries. Run from the repository root:

```sh
arduino-cli compile --profile nano_old --warnings all Talking_Scale
```

Use `--profile nano` for the newer Nano bootloader. To upload, use the same
profile and supply the board's serial port, for example:

```sh
arduino-cli upload --profile nano_old --port /dev/cu.usbserial-XXXX Talking_Scale
```

Serial diagnostics use 9600 baud. An existing four-byte calibration float at
EEPROM address 0 is imported once into two alternating, versioned records with
CRC16 checksums. Each save commits the new record last, preserving the previous
record if power fails mid-write. The old float is retained, but is never used as
a fallback after journal initialization, so corrupt records cannot silently
revive stale calibration. User settings use separate bytes. Erased EEPROM, zero,
NaN, and infinity require calibration; the scale announces "error calibrate" and
selects calibration in the menu instead of speaking invalid measurements.

## Controls

The scale zeroes the empty platform at startup, then speaks a rolling average of
20 filtered conversions with two decimal digits. A median of three rejects
isolated spikes before averaging. The window updates on every conversion,
keeping adjustment feedback current.

| Gesture                                                            | Action                                                            |
| ------------------------------------------------------------------ | ----------------------------------------------------------------- |
| Press                                                              | Interrupt the current word after 30 ms of stable contact          |
| Hold for about one second                                          | Enter or leave the menu; entering announces "set"                 |
| Single click in the menu                                           | Cycle through "zero weight", "calibrate", and "control"           |
| Double click in the menu                                           | Run the operation, or cycle the announcement mode under "control" |
| Hold during a pending tare or calibration                          | Cancel it and leave the menu                                      |
| Hold the button before switching on; keep holding for five seconds | Erase EEPROM, including calibration and the announcement mode     |

Allow about 600 ms after releasing a single click for double-click detection.
Gestures work during speech and sensor sampling. A press stops synthesis
immediately after debounce, ramps the output down, and discards the rest of that
announcement. Measurement speech stays paused until the gesture is resolved; a
hold may announce its menu action while the button is still held. A short press
outside the menu silences the old reading, then weighing resumes with a fresh
reading. Startup zeroing must finish before entering the menu.

To clear calibration and settings completely, switch the scale off, hold the
button down, and switch it on again with the platform empty. Keep holding until
it says "zero", then release. The five-second confirmation starts when the
sketch runs, after any bootloader delay; erasing can take a few more seconds.
Releasing before the five seconds cancels the reset and preserves the settings.
A hold that starts after startup uses the normal menu controls.

The reset erases and verifies all 1024 EEPROM bytes. It then starts with Repeat
mode and no calibration. After startup zeroing it says "error calibrate" and
selects calibration; double click to begin the normal 5 g calibration procedure.
The reset hold cannot open the menu accidentally after erasure. An interrupted
erase resumes automatically on the next startup before any settings are loaded.
"Error fail" indicates an erase verification failure; settings are not used in
that state. A power cycle retries an erase that already has its resume marker.

The "control" menu item speaks the current announcement mode. Double click to
cycle modes, then hold to return to weighing. The mode is saved separately from
calibration and defaults to Repeat if its settings record is missing or invalid.

| Mode   | Behaviour                                                                                               |
| ------ | ------------------------------------------------------------------------------------------------------- |
| Repeat | Repeated readings with a two-second pause after each completed measurement; this is the initial default |
| Change | Speak settled values when their rounded hundredths change; repeat after 30 seconds of silence           |
| Manual | Speak a fresh averaged reading only when a short press is completed                                     |

A short press requests a repeat in Change mode too, after settling. Errors and
calibration instructions remain audible in every mode. The previous value is
acknowledged only after the entire announcement finishes; interrupting it does
not mark it as heard. Tare, calibration, menu transitions, and sensor recovery
reset announcement history. The new words "control", "repeat", "change", and
"manual" are original VM61002 samples matching the existing number voice.

To tare, select "zero weight" and double click. The scale fills the filtered
window and requires stability for another 500 ms before announcing "zero". If it
cannot settle within 15 seconds, it announces "error" and keeps the old zero.
Startup also requires 500 ms of stability, but uses a factor-independent noise
check. It continues waiting after an error if the platform is moving or the
sensor is faulty. Hold to leave the menu after a user-requested tare.

To calibrate:

1. Empty the platform, enter the menu, select "calibrate", and double click.
   Keep it empty until "put five"; baseline collection takes about four seconds
   on a 10-sample-per-second HX711 module.
2. Wait for "put five". After the prompt finishes, there are two seconds to
   place the 5 g reference weight. Leave it still while two sets of 20 filtered
   readings are compared (about four more seconds at 10 samples per second).
3. "Weight five" confirms that the factor was saved. Remove the reference weight
   before weighing another item.

A short press during "put five" silences it and replays the full prompt after
the gesture is resolved. The placement countdown waits for the replay to finish.
A one-second hold cancels calibration as usual.

When startup has already selected calibration after "error calibrate", double
click to start it. A cancelled, zero-result, or interrupted calibration leaves
the previous factor and offset intact. Calibration compares the repeatability of
independent, median-filtered averages rather than requiring every raw sample to
fit within 0.01 g. The empty baseline does not depend on the old factor. The
reference signal must exceed both 32 raw counts and ten times the filtered noise
range; the baseline and loaded means must repeat within 0.01 g under the
candidate factor. Each collection phase allows up to 15 seconds. A pending
button gesture delays the save so a cancellation hold can finish first.

Failure says "calibrate fail" and returns to the calibration menu item. The
original ERROR sample is a short vowel-heavy word; FAIL is a clearer matching
VM61002 sample. Sensor recovery waits for a pending notification to finish,
rather than interrupting it with "ready". The `calibration` USB command reports
whether the last attempt lacked a signal, had a moving baseline or weight,
encountered a sensor fault, or failed to save. A baseline that proves unstable
under the candidate factor fails promptly so it can be remeasured empty. A
negative factor remains valid for reversed load-cell polarity, and the correct 5
g reference must still be used; software cannot identify an incorrect weight.

If the HX711 stops providing conversions for three seconds, the scale announces
"error", stops reporting measurements, and keeps polling the button and sensor.
Clipped conversions at either ADC limit are discarded immediately. Three
consecutive clipped conversions announce a sensor error in every speech mode.
Recovery requires five consecutive valid conversions, followed by a fresh
filtered window. Startup zeroing resumes when the sensor recovers; interrupted
user operations must be selected again. ADC clipping does not establish the load
cell's mechanical overload limit.

## Software changes and verification

The old hundredths calculation captured both fractional digits and divided them
by ten, repeating the tenths digit: 1.27 became "one point two two". Readings
are now rounded once to integer hundredths and split into separate digits. This
also handles negative readings below one gram, trailing zeroes, and rounding
carry. Number speech uses 32-bit values rather than AVR's 16-bit `int`.

Speech uses Talkie's asynchronous API with a complete phrase queued at once. The
bounded phrase and silent boundary words fit its FIFO, avoiding both blocking
fills and speaker output resets between words. `SpeechPlayback.h` provides a
version-specific adapter for Talkie 1.4.0 and the 16 MHz ATmega328P. It starts
synthesis with D3 disconnected, clears residual synthesis state with a silent
preroll, and ramps PWM bias up before speech. A silent tail holds the output
active while it ramps back to zero, then releases D3. Button cancellation uses
the same nonblocking ramp-down. Ramp steps are limited even if polling is
delayed by an EEPROM write. The installed Talkie library is unchanged. Ready
sensor conversions are accumulated across loop iterations; tare, calibration,
and the placement interval also run without long waits. The direct button
handler uses `millis()` with no additional interrupt or timer. On this Nano,
Talkie uses timers 1 and 2 while `millis()` uses timer 0. The current HX711
library protects its clock pulses from interrupt stretching. Timer1 is suspended
during the ramps and pending compares are cleared before resuming; Timer0
remains available for button timing. The final Talkie state reset preserves the
previous interrupt state.

The speech buffer holds 20 word pointers; the longest supported reading needs
17. Exhaustive tests check the bound for every whole number below one million.
Nano build sizes with the same AVR core and speech libraries:

| Resource   | Before direct button | Direct button only | Current software |
| ---------- | -------------------: | -----------------: | ---------------: |
| Flash      | 14,172 bytes         | 13,378 bytes       | 25,446 bytes     |
| Static RAM | 545 bytes            | 452 bytes          | 1,043 bytes      |

The menu wraps back to "zero weight". Calibration is applied only after stable
measurements and verified EEPROM writes. Journal records use byte updates,
version checks, CRC16, and readback; failed writes preserve the previous factor.

Run the host regression tests with:

```sh
tests/run-tests.sh
```

This requires a C++11 compiler and no installed Arduino libraries.

These tests cover decimal speech, button debounce and gesture timing (including
clock rollover), immediate speech interruption, menu cycling, calibration,
announcement modes, sensor recovery, noisy startup and calibration, and
operation with serial output unavailable. Startup reset tests cover
confirmation, early release, full erasure, interrupted erasure, and failed
writes. Startup tests also check that unrealistic saved factors cannot block
startup or admit a moving zero, and that calibration can be corrected without an
EEPROM reset. Storage tests simulate interrupted and silently failed writes at
each affected byte. They complement the Nano compilation; they do not simulate
the AVR audio interrupt, analogue noise, or load-cell accuracy. Compiler
warnings from the current HX711 library and AVR core may appear with
`--warnings all`.

Before relying on a flashed scale, check speech quality and single/double/long
presses while it speaks. Verify phrase starts, endings, and interruptions on the
physical speaker; host tests cannot establish the audible result. Calibrate with
5 g, verify several known loads including hundredths, test tare and a negative
reading, and power-cycle to confirm the calibration is retained. Speaking
hundredths does not establish 0.01 g physical accuracy; that also depends on the
load cell, calibration, and mechanical setup.

Weighing/tare stability compares four chronological five-sample block means
within the rolling window, allowing at most 0.01 g of movement (with a two-count
minimum). Calibration compares independent 20-sample means. Startup zeroing uses
the four block means too, but sets its tolerance from background noise without
using the saved factor. The second-smallest within-block range estimates noise;
the tolerance is 1.5 times that range, with a two-count minimum and a 4096-count
ceiling. One or two blocks containing a transient do not set the noise estimate.
The normal 500 ms settling interval still applies. These checks permit
repeatable averages despite broader individual-sample noise and reject sustained
drift. The limits in `ScaleFilter.h` and `CalibrationCheck.h` should be checked
against USB sample logs. Zero is not automatically adjusted during weighing.

## USB diagnostics

Open a serial terminal at 9600 baud and send lowercase commands terminated by
newline (CRLF is also accepted):

| Command           | Result                                                                  |
| ----------------- | ----------------------------------------------------------------------- |
| `help`            | List commands                                                           |
| `status` or `raw` | One diagnostic snapshot                                                 |
| `calibration`     | Last calibration status, noise ranges and movement of averaged readings |
| `stream on`       | Enable snapshots at up to 5 Hz                                          |
| `stream off`      | Stop streaming and return a final snapshot                              |

Snapshots contain `raw`, filtered `mean`, grams (`g`), filtered peak-to-peak
noise (`spread`, raw counts), averaged-block movement (`motion`), `stable`, zero
offset (`zero`), `net` counts after zero, calibration `factor`, measurement
validity (`valid`), operation (`op`), sensor `fault`, speech `mode`, and sample
age (`age`, milliseconds). Grams are `na` when there is no valid weighing
window. `net` shows zero drift only while the platform is empty; with a load it
represents the load signal.

Operation codes are 0 startup zero, 1 weighing, 2 user zero, 3 calibration zero,
4 calibration prompt, 5 placement wait, 6 reference measurement, and 7 pending
calibration save. Fault codes are 0 healthy, 1 conversion timeout, 2 clipping.
Mode codes are 0 Repeat, 1 Change, 2 Manual.

Diagnostics are read-only and silent until requested. Streaming sends snapshots,
not every ADC conversion. Input processing and output draining are bounded per
loop; a full output buffer cannot block the button, speech or sensor polling.
Requests may be coalesced while output is busy. Oversized and unknown commands
are rejected, and all buffers have fixed sizes.

During calibration (including its failure menu), streaming alternates normal
snapshots with `cal=` records. These report a readable status, `timedout`,
operation `op`, latest `raw`, current-window sample count `n`, empty mean
`base`, empty filtered noise span `bs`, movement between empty means `bm`,
reference mean `load`, reference noise span `ls`, movement between loaded means
`lm`, and the tentative `candidate` factor. All spans and movements are in ADC
counts. A candidate is not applied or saved until validation and EEPROM readback
pass.
