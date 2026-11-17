#ifndef SENSOR_HEALTH_H
#define SENSOR_HEALTH_H

#include <stdint.h>

enum class SensorSample : uint8_t { Accept, Reject, Fault, Recovered };

class SensorHealth {
public:
  SensorSample add(int32_t raw) {
    if (raw >= 8388607L || raw <= -8388608L) {
      goodSamples = 0;
      if (railSamples < CLIPPING_SAMPLES) ++railSamples;
      if (railSamples == CLIPPING_SAMPLES && !clippingFault) {
        fault = true;
        clippingFault = true;
        return SensorSample::Fault;
      }
      // Discard even an isolated clipped conversion before it enters an average.
      return SensorSample::Reject;
    }

    railSamples = 0;
    if (!fault) return SensorSample::Accept;

    if (goodSamples < RECOVERY_SAMPLES) ++goodSamples;
    if (goodSamples < RECOVERY_SAMPLES) return SensorSample::Reject;

    fault = false;
    clippingFault = false;
    goodSamples = 0;
    return SensorSample::Recovered;
  }

  void timeout() {
    fault = true;
    clippingFault = false;
    railSamples = 0;
    goodSamples = 0;
  }

  bool faulted() const { return fault; }
  bool clipped() const { return clippingFault; }

private:
  static const uint8_t CLIPPING_SAMPLES = 3;
  static const uint8_t RECOVERY_SAMPLES = 5;
  uint8_t railSamples = 0;
  uint8_t goodSamples = 0;
  bool fault = false;
  bool clippingFault = false;
};

#endif
