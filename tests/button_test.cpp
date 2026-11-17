#include "../Talking_Scale/ScaleButton.h"

#include <assert.h>
#include <stdio.h>

void expect(ScaleButton &button, bool pressed, uint32_t at, ButtonEvent event) {
  assert(button.update(pressed, at) == event);
}

void testSingleClick() {
  ScaleButton button;
  assert(!button.busy());
  expect(button, true, 0, ButtonEvent::None);
  assert(button.busy());
  expect(button, true, 29, ButtonEvent::None);
  expect(button, true, 30, ButtonEvent::Down);
  expect(button, false, 80, ButtonEvent::None);
  assert(button.busy());
  expect(button, false, 109, ButtonEvent::None);
  expect(button, false, 110, ButtonEvent::None);
  assert(button.busy());
  expect(button, false, 709, ButtonEvent::None);
  expect(button, false, 710, ButtonEvent::Click);
  assert(!button.busy());
  expect(button, false, 5000, ButtonEvent::None);
}

void testBounce() {
  ScaleButton button;
  expect(button, true, 0, ButtonEvent::None);
  expect(button, false, 10, ButtonEvent::None);
  expect(button, true, 20, ButtonEvent::None);
  expect(button, true, 49, ButtonEvent::None);
  expect(button, true, 50, ButtonEvent::Down);
  expect(button, false, 100, ButtonEvent::None);
  expect(button, true, 110, ButtonEvent::None);
  expect(button, false, 120, ButtonEvent::None);
  expect(button, false, 149, ButtonEvent::None);
  expect(button, false, 150, ButtonEvent::None);
  expect(button, false, 750, ButtonEvent::Click);
  assert(!button.busy());

  ScaleButton tap;
  expect(tap, true, 0, ButtonEvent::None);
  expect(tap, false, 29, ButtonEvent::None);
  assert(!tap.busy());
  expect(tap, false, 1000, ButtonEvent::None);
}

void testDoubleClick() {
  ScaleButton button;
  expect(button, true, 0, ButtonEvent::None);
  expect(button, true, 30, ButtonEvent::Down);
  expect(button, false, 80, ButtonEvent::None);
  expect(button, false, 110, ButtonEvent::None);
  expect(button, true, 200, ButtonEvent::None);
  expect(button, true, 230, ButtonEvent::Down);
  expect(button, false, 280, ButtonEvent::None);
  expect(button, false, 310, ButtonEvent::DoubleClick);
  assert(!button.busy());
  expect(button, false, 2000, ButtonEvent::None);
}

void testClickWindowEdge() {
  ScaleButton button;
  expect(button, true, 0, ButtonEvent::None);
  expect(button, true, 30, ButtonEvent::Down);
  expect(button, false, 80, ButtonEvent::None);
  expect(button, false, 110, ButtonEvent::None);
  // Physical contact before the deadline counts even when debounce ends later.
  expect(button, true, 709, ButtonEvent::None);
  expect(button, true, 710, ButtonEvent::None);
  expect(button, true, 739, ButtonEvent::Down);
  expect(button, false, 780, ButtonEvent::None);
  expect(button, false, 810, ButtonEvent::DoubleClick);

  ScaleButton late;
  expect(late, true, 0, ButtonEvent::None);
  expect(late, true, 30, ButtonEvent::Down);
  expect(late, false, 80, ButtonEvent::None);
  expect(late, false, 110, ButtonEvent::None);
  // At the deadline the old click expires; the new press remains independent.
  expect(late, true, 710, ButtonEvent::Click);
  assert(late.busy());
  expect(late, true, 739, ButtonEvent::None);
  expect(late, true, 740, ButtonEvent::Down);
  expect(late, false, 800, ButtonEvent::None);
  expect(late, false, 830, ButtonEvent::None);
  expect(late, false, 1430, ButtonEvent::Click);
  assert(!late.busy());
}

void testLongPress() {
  ScaleButton button;
  expect(button, true, 0, ButtonEvent::None);
  expect(button, true, 30, ButtonEvent::Down);
  expect(button, true, 1029, ButtonEvent::None);
  expect(button, true, 1030, ButtonEvent::LongPress);
  assert(button.busy());
  expect(button, true, 5000, ButtonEvent::None);
  expect(button, false, 5010, ButtonEvent::None);
  expect(button, false, 5040, ButtonEvent::None);
  assert(!button.busy());
  expect(button, false, 10000, ButtonEvent::None);

  // A released button must not turn into a hold during release debounce.
  ScaleButton almost;
  expect(almost, true, 0, ButtonEvent::None);
  expect(almost, true, 30, ButtonEvent::Down);
  expect(almost, false, 1029, ButtonEvent::None);
  expect(almost, false, 1030, ButtonEvent::None);
  expect(almost, false, 1059, ButtonEvent::None);
  expect(almost, false, 1659, ButtonEvent::Click);
}

void testClickThenHold() {
  ScaleButton button;
  expect(button, true, 0, ButtonEvent::None);
  expect(button, true, 30, ButtonEvent::Down);
  expect(button, false, 80, ButtonEvent::None);
  expect(button, false, 110, ButtonEvent::None);
  expect(button, true, 200, ButtonEvent::None);
  expect(button, true, 230, ButtonEvent::Down);
  expect(button, true, 1000, ButtonEvent::None);
  expect(button, true, 1230, ButtonEvent::LongPress);
  expect(button, false, 1300, ButtonEvent::None);
  expect(button, false, 1330, ButtonEvent::None);
  assert(!button.busy());
  expect(button, false, 3000, ButtonEvent::None);
}

void testRollover() {
  ScaleButton button;
  const uint32_t start = UINT32_MAX - 20;
  expect(button, true, start, ButtonEvent::None);
  expect(button, true, start + 29, ButtonEvent::None);
  expect(button, true, start + 30, ButtonEvent::Down);
  expect(button, true, start + 1030, ButtonEvent::LongPress);
  expect(button, false, start + 1100, ButtonEvent::None);
  expect(button, false, start + 1130, ButtonEvent::None);
  assert(!button.busy());

  ScaleButton click;
  const uint32_t nearEnd = UINT32_MAX - 300;
  expect(click, true, nearEnd, ButtonEvent::None);
  expect(click, true, nearEnd + 30, ButtonEvent::Down);
  expect(click, false, nearEnd + 80, ButtonEvent::None);
  expect(click, false, nearEnd + 110, ButtonEvent::None);
  expect(click, false, nearEnd + 709, ButtonEvent::None);
  expect(click, false, nearEnd + 710, ButtonEvent::Click);
  assert(!click.busy());
}

int main() {
  testSingleClick();
  testBounce();
  testDoubleClick();
  testClickWindowEdge();
  testLongPress();
  testClickThenHold();
  testRollover();
  printf("Button tests passed (state size: %zu bytes).\n", sizeof(ScaleButton));
}
