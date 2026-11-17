#ifndef STARTUP_HOLD_H
#define STARTUP_HOLD_H

#include <stdint.h>

enum class StartupHoldResult : uint8_t { Inactive, Pending, Cancelled, Confirmed };

class StartupHold {
public:
  // Call once at firmware startup. A press that starts later cannot arm it.
  void begin(bool pressed, uint32_t now) {
    active = pressed;
    startedAt = now;
  }

  StartupHoldResult update(bool pressed, uint32_t now) {
    if (!active) return StartupHoldResult::Inactive;
    // Release wins even at the deadline: confirmation requires a held button.
    if (!pressed) {
      active = false;
      return StartupHoldResult::Cancelled;
    }
    if (now - startedAt >= HOLD_MS) {
      active = false;
      return StartupHoldResult::Confirmed;
    }
    return StartupHoldResult::Pending;
  }

private:
  static const uint32_t HOLD_MS = 5000;
  uint32_t startedAt = 0;
  bool active = false;
};

#endif
