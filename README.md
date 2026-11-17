# Legacy Talking Scale

A talking tracking force scale using a 16 MHz ATmega328P Arduino Nano, an HX711,
and Talkie speech synthesis. It was created to let a visually impaired person
set a turntable cartridge's tracking force. This version updates the software
for the original hardware and retains its speech samples.

The [original build instructions](https://theblindmansworkshop.com/build-a-talking-scale-with-an-arduino-hx711-and-the-talkie-library/)
describe the enclosure, load cell, power supply, and wiring.

## Hardware

The pin assignments are unchanged:

| Connection | Arduino pin |
| --- | --- |
| Button to ground, using the internal pull-up | D2 |
| Speaker positive; speaker negative to ground | D3 |
| HX711 DT / DOUT | D4 |
| HX711 SCK | D5 |

Talkie's inverted D11 output is disabled. The sketch requires a 16 MHz
ATmega328P: Talkie's 8 MHz mode disables the clock used to time button gestures.
The same pin mapping also works on a 16 MHz ATmega328P Uno.

## Libraries and building

Use these tested releases from Arduino Library Manager:

| Library | Version | Maintainer |
| --- | --- | --- |
| [Talkie](https://github.com/ArminJo/Talkie) | 1.4.0 | Armin Joachimsmeyer |
| [HX711 Arduino Library](https://github.com/bogde/HX711) | 0.7.5 | Bogdan Necula |
| [OneButton](https://github.com/mathertel/OneButton) | 2.6.2 | Matthias Hertel |

Choose **HX711 Arduino Library**; the library named just **HX711** has a different
API. EEPROM support comes from the Arduino AVR core; EEPROMEx is no longer needed.
The tested Arduino AVR Boards core is 1.8.8.

For the Arduino IDE, move the old bundled libraries out of the sketchbook's
`libraries` folder before installing the releases above. Open
`Talking_Scale/Talking_Scale.ino`, select Arduino Nano and the matching ATmega328P
processor/bootloader, then verify and upload. Many older Nano clones need
**ATmega328P (Old Bootloader)**. Keep the platform empty during startup.

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

Serial diagnostics use 9600 baud. The calibration factor remains a four-byte
float at EEPROM address 0, so a valid calibration from the old software survives
an ordinary sketch upload. Erased EEPROM, zero, NaN, and infinity require
calibration; the scale announces "error calibrate" and selects calibration in
the menu instead of speaking invalid measurements.

## Controls

The scale zeroes the empty platform at startup, then repeatedly speaks averages
of 20 sensor conversions with two decimal digits.

| Gesture | Action |
| --- | --- |
| Hold for about one second | Enter or leave the menu; entering announces "set" |
| Single click in the menu | Cycle between "zero weight" and "calibrate" |
| Double click in the menu | Run the selected operation |
| Hold during a pending tare or calibration | Cancel it and leave the menu |

Allow about 600 ms after a single click for double-click detection. Gestures work
during speech and sensor sampling. A menu prompt replaces the remaining words
of a measurement after the word currently playing finishes. Startup zeroing
must finish before entering the menu.

To tare, select "zero weight" and double click. The scale averages ten
conversions and announces "zero". Hold to leave the menu and resume weighing.

To calibrate:

1. Empty the platform, enter the menu, select "calibrate", and double click.
2. Wait for "put five". After the prompt finishes, there are two seconds to place
   the 5 g reference weight. Leave it still while 20 conversions are averaged.
3. "Weight five" confirms that the factor was saved. Remove the reference weight
   before weighing another item.

When startup has already selected calibration after "error calibrate", double
click to start it. A cancelled, zero-result, or interrupted calibration leaves
the previous factor and offset intact. A negative factor is valid when load-cell
polarity is reversed. The routine assumes the correct reference weight was
placed; it cannot identify a wrong or missing weight from noisy readings alone.

If the HX711 stops providing conversions for three seconds, the scale announces
"error", stops reporting measurements, and keeps polling the button and sensor.
Startup zeroing resumes when the sensor recovers; interrupted user operations
must be selected again.

## Software changes and verification

The old hundredths calculation captured both fractional digits and divided them
by ten, repeating the tenths digit: 1.27 became "one point two two". Readings are
now rounded once to integer hundredths and split into separate digits. This also
handles negative readings below one gram, trailing zeroes, and rounding carry.
Number speech uses 32-bit values rather than AVR's 16-bit `int`.

Speech uses Talkie's asynchronous API, feeding one word at a time so its FIFO
cannot fill and block button polling. Ready sensor conversions are accumulated
across loop iterations; tare, calibration, and the placement interval also run
without long waits. OneButton needs no additional timer. On this Nano, Talkie
uses timers 1 and 2 while `millis()` and OneButton use timer 0. The current HX711
library protects its clock pulses from interrupt stretching.

The menu now wraps correctly back to "zero weight". EEPROM writes use the core's
update-on-change `put` operation, and calibration is applied only after a valid
result is available.

Run the host regression tests with:

```sh
tests/run-tests.sh
```

This requires a C++14 compiler and OneButton 2.6.2 installed in the standard
Arduino sketchbook. For another location, set `ONEBUTTON_SOURCE_DIR` to the
library's `src` directory when running the script.

These tests cover decimal speech, real OneButton gesture handling with simulated
speech and sensor timing, menu cycling, calibration, and sensor recovery. They
complement the Nano compilation; they do not simulate the AVR audio interrupt,
analogue noise, or load-cell accuracy. Compiler warnings from the current HX711
library and AVR core may appear with `--warnings all`.

Before relying on a flashed scale, check speech quality and single/double/long
presses while it speaks, calibrate with 5 g, verify several known loads including
hundredths, test tare and a negative reading, and power-cycle to confirm the
calibration is retained. Speaking hundredths does not establish 0.01 g physical
accuracy; that also depends on the load cell, calibration, and mechanical setup.
