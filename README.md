# Legacy Talking Scale
 A talking scale based on the 328-based Arduino platform, HX711 and the Talkie text to speech library.

This is a talking weight scale using the HX711 sensor and a 328-based Arduino. The scale was created to allow a visually impaired person (me) a means to alter the tracking force of a turntable stylus. However the software will work equally well with any combination of load cells so could with small modifications be used to build larger scales with spoken output.

This is legacy code. There is a version in the works with significant hardware changes, so I am publishing this in a separate repository. The current iteration is in development and switches to sample-based text to speech. The Talkie library is great, but it has some limitations running on comparatively limited microcontrollers; namely its tendency to hog timers and halt code execution while it speaks. It is also time-consuming to generate LPC speech data that is intelligible, which makes adding new words, and thus new functions a challenge.

It does however have the advantage of requiring very little hardware, so I am publishing this code here for those who wish to build the talking scale using software speech and to serve as an example that such a project can certainly be achieved. I still welcome contributions and will fix reported bugs.

You can view a full change-log, instructions for the build and the current status of the project at [My Website](https://ashleycox.co.uk/projects/talking-stylus-tracking-force-scale/)

# Libraries

This code relies on the [talkie-fix](https://github.com/norberthidas/Talkie-FIX), [HX711](https://github.com/bogde/HX711), [EEPROMEx](https://github.com/thijse/Arduino-EEPROMEx) and [oneButton](https://github.com/mathertel/OneButton) libraries. 