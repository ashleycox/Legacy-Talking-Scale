#include "../Talking_Scale/AnnouncementPolicy.h"

#include <assert.h>
#include <stdio.h>

void testRepeat() {
  AnnouncementPolicy policy;
  assert(policy.mode() == AnnouncementMode::Repeat);
  assert(policy.shouldSpeak(127, false, 0));
  policy.completed(127, 1000);
  assert(policy.shouldSpeak(127, false, 1001));
  assert(policy.shouldSpeak(-1, true, 1001));
}

void testChange() {
  AnnouncementPolicy policy;
  policy.setMode(AnnouncementMode::Change);
  assert(!policy.shouldSpeak(127, false, 0));
  assert(policy.shouldSpeak(127, true, 0));
  // Starting a phrase without completing it does not acknowledge that value.
  assert(policy.shouldSpeak(127, true, 5000));
  policy.completed(127, 5000);
  assert(!policy.shouldSpeak(127, true, 5001));
  assert(policy.shouldSpeak(128, true, 5001)); // A real 0.01 g change is retained.
  assert(policy.shouldSpeak(126, true, 5001));
  assert(!policy.shouldSpeak(128, false, 5001));
  // An interrupted changed reading leaves the last fully spoken value intact.
  assert(policy.shouldSpeak(128, true, 6000));
  assert(!policy.shouldSpeak(127, true, 6000));
  policy.completed(128, 6000);
  assert(!policy.shouldSpeak(128, true, 6001));
  assert(policy.shouldSpeak(-1, true, 6001));
}

void testReminders() {
  AnnouncementPolicy policy;
  policy.setMode(AnnouncementMode::Change);
  policy.completed(200, 1000);
  assert(!policy.shouldSpeak(200, true, 30999));
  assert(policy.shouldSpeak(200, true, 31000));
  assert(!policy.shouldSpeak(200, false, 31000));
  // A reminder interrupted by the button remains due.
  assert(policy.shouldSpeak(200, true, 32000));
  policy.completed(200, 33000);
  assert(!policy.shouldSpeak(200, true, 33001));
  assert(policy.shouldSpeak(200, true, 63000));
}

void testExplicitChangeRepeat() {
  AnnouncementPolicy policy;
  policy.setMode(AnnouncementMode::Change);
  policy.completed(200, 1000);
  policy.request();
  assert(!policy.shouldSpeak(200, false, 1001));
  assert(policy.shouldSpeak(200, true, 1001));
  assert(policy.shouldSpeak(200, true, 1002));
  policy.completed(200, 1100);
  assert(!policy.shouldSpeak(200, true, 1101));
}

void testManual() {
  AnnouncementPolicy policy;
  policy.setMode(AnnouncementMode::Manual);
  assert(!policy.shouldSpeak(127, true, 0));
  assert(!policy.shouldSpeak(200, true, 60000));
  policy.request();
  assert(policy.shouldSpeak(127, false, 60001));
  assert(policy.shouldSpeak(200, true, 60002));
  policy.completed(200, 61000);
  assert(!policy.shouldSpeak(200, true, 61001));
  assert(!policy.shouldSpeak(300, true, 120000));
  policy.request();
  assert(policy.shouldSpeak(300, false, 120001));
  policy.reset();
  assert(policy.mode() == AnnouncementMode::Manual);
  assert(!policy.shouldSpeak(300, true, 120002));
}

void testResetAndModeValidation() {
  AnnouncementPolicy policy;
  policy.setMode(AnnouncementMode::Change);
  policy.completed(100, 0);
  assert(!policy.shouldSpeak(100, true, 1));
  policy.reset();
  assert(policy.mode() == AnnouncementMode::Change);
  assert(policy.shouldSpeak(100, true, 1));
  policy.request();
  policy.setMode(AnnouncementMode::Manual);
  assert(!policy.shouldSpeak(100, true, 2));
  policy.setMode(AnnouncementMode::Change);
  assert(policy.shouldSpeak(100, true, 3));
  assert(validAnnouncementMode(AnnouncementMode::Repeat));
  assert(validAnnouncementMode(AnnouncementMode::Change));
  assert(validAnnouncementMode(AnnouncementMode::Manual));
  assert(!validAnnouncementMode(static_cast<AnnouncementMode>(3)));
  assert(!validAnnouncementMode(static_cast<AnnouncementMode>(255)));
  policy.setMode(static_cast<AnnouncementMode>(255));
  assert(policy.mode() == AnnouncementMode::Repeat);
  assert(policy.shouldSpeak(100, false, 4));
}

void testRollover() {
  AnnouncementPolicy policy;
  policy.setMode(AnnouncementMode::Change);
  const uint32_t completedAt = UINT32_MAX - 1000;
  policy.completed(200, completedAt);
  assert(!policy.shouldSpeak(200, true, completedAt + 29999));
  assert(policy.shouldSpeak(200, true, completedAt + 30000));
  assert(policy.shouldSpeak(201, true, completedAt + 1));
}

int main() {
  testRepeat();
  testChange();
  testReminders();
  testExplicitChangeRepeat();
  testManual();
  testResetAndModeValidation();
  testRollover();
  puts("Announcement policy tests passed.");
}
