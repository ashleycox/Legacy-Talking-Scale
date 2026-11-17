#ifndef SCALE_BUTTON_H
#define SCALE_BUTTON_H

#include <stdint.h>

enum class ButtonEvent : uint8_t { None, Down, Click, DoubleClick, LongPress };

class ScaleButton {
public:
  ButtonEvent update(bool pressed, uint32_t now) {
    if (pressed != rawPressed) {
      rawPressed = pressed;
      rawChangedAt = now;
    }

    // A second press begun inside the click window may finish debouncing after
    // its deadline. A later press first delivers the preceding single click.
    if (clickPending && !stablePressed && now - gestureAt >= CLICK_MS &&
        (!rawPressed || rawChangedAt - gestureAt >= CLICK_MS)) {
      clickPending = false;
      return ButtonEvent::Click;
    }

    if (rawPressed != stablePressed && now - rawChangedAt >= DEBOUNCE_MS) {
      stablePressed = rawPressed;
      if (stablePressed) {
        gestureAt = now;
        longPressSent = false;
        return ButtonEvent::Down;
      }
      if (longPressSent) {
        longPressSent = false;
        return ButtonEvent::None;
      }
      if (clickPending) {
        clickPending = false;
        return ButtonEvent::DoubleClick;
      }
      clickPending = true;
      gestureAt = now;
    }

    if (stablePressed && rawPressed && !longPressSent &&
        now - gestureAt >= LONG_PRESS_MS) {
      longPressSent = true;
      clickPending = false;
      return ButtonEvent::LongPress;
    }
    return ButtonEvent::None;
  }

  // Include unsettled contacts and the click window, so automatic speech cannot
  // restart in the middle of a gesture. Explicit menu prompts may still speak.
  bool busy() const { return rawPressed || stablePressed || clickPending; }

private:
  static const uint32_t DEBOUNCE_MS = 30;
  static const uint32_t CLICK_MS = 600;
  static const uint32_t LONG_PRESS_MS = 1000;
  uint32_t rawChangedAt = 0;
  uint32_t gestureAt = 0;
  bool rawPressed = false;
  bool stablePressed = false;
  bool longPressSent = false;
  bool clickPending = false;
};

#endif
