#include "../Talking_Scale/StartupHold.h"

#include <assert.h>
#include <stdio.h>

void testConfirmation() {
  StartupHold hold;
  hold.begin(true, 100);
  assert(hold.update(true, 100) == StartupHoldResult::Pending);
  assert(hold.update(true, 5099) == StartupHoldResult::Pending);
  assert(hold.update(true, 5100) == StartupHoldResult::Confirmed);
  assert(hold.update(true, 5101) == StartupHoldResult::Inactive);
  assert(hold.update(false, 5200) == StartupHoldResult::Inactive);
  assert(hold.update(true, 20000) == StartupHoldResult::Inactive);
}

void testEarlyRelease() {
  StartupHold hold;
  hold.begin(true, 100);
  assert(hold.update(true, 4999) == StartupHoldResult::Pending);
  assert(hold.update(false, 5000) == StartupHoldResult::Cancelled);
  assert(hold.update(true, 5001) == StartupHoldResult::Inactive);
  assert(hold.update(true, 30000) == StartupHoldResult::Inactive);

  StartupHold immediate;
  immediate.begin(true, 0);
  assert(immediate.update(false, 0) == StartupHoldResult::Cancelled);

  StartupHold deadline;
  deadline.begin(true, 0);
  assert(deadline.update(false, 5000) == StartupHoldResult::Cancelled);
  assert(deadline.update(true, 5001) == StartupHoldResult::Inactive);
}

void testUnarmed() {
  StartupHold hold;
  assert(hold.update(true, 20000) == StartupHoldResult::Inactive);
  hold.begin(false, 100);
  assert(hold.update(false, 100) == StartupHoldResult::Inactive);
  assert(hold.update(true, 200) == StartupHoldResult::Inactive);
  assert(hold.update(true, 100000) == StartupHoldResult::Inactive);
}

void testRollover() {
  StartupHold hold;
  const uint32_t start = UINT32_MAX - 2000;
  hold.begin(true, start);
  assert(hold.update(true, start + 4999) == StartupHoldResult::Pending);
  assert(hold.update(true, start + 5000) == StartupHoldResult::Confirmed);
  assert(hold.update(true, start + 5001) == StartupHoldResult::Inactive);

  StartupHold cancelled;
  cancelled.begin(true, start);
  assert(cancelled.update(false, start + 3000) == StartupHoldResult::Cancelled);
  assert(cancelled.update(true, start + 10000) == StartupHoldResult::Inactive);
}

int main() {
  testConfirmation();
  testEarlyRelease();
  testUnarmed();
  testRollover();
  puts("Startup hold tests passed.");
}
