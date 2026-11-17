#ifndef ANNOUNCEMENT_POLICY_H
#define ANNOUNCEMENT_POLICY_H

#include <stdint.h>

enum class AnnouncementMode : uint8_t { Repeat = 0, Change = 1, Manual = 2 };

inline bool validAnnouncementMode(AnnouncementMode mode) {
  return static_cast<uint8_t>(mode) <= static_cast<uint8_t>(AnnouncementMode::Manual);
}

class AnnouncementPolicy {
public:
  AnnouncementMode mode() const { return selectedMode; }

  void setMode(AnnouncementMode mode) {
    selectedMode = validAnnouncementMode(mode) ? mode : AnnouncementMode::Repeat;
    reset();
  }

  void reset() {
    hasCompleted = false;
    requested = false;
  }

  void request() { requested = true; }

  bool shouldSpeak(int32_t hundredths, bool settled, uint32_t now) const {
    if (selectedMode == AnnouncementMode::Repeat) return true;
    if (selectedMode == AnnouncementMode::Manual) return requested;

    // Explicit repeats in Change mode still wait for a settled measurement.
    return settled && (requested || !hasCompleted || hundredths != lastCompleted ||
                       now - lastCompletedAt >= REMINDER_MS);
  }

  void completed(int32_t hundredths, uint32_t now) {
    // Queueing or interrupting a phrase must not call this: only finished speech
    // establishes what the user actually heard and satisfies an explicit request.
    lastCompleted = hundredths;
    lastCompletedAt = now;
    hasCompleted = true;
    requested = false;
  }

private:
  static const uint32_t REMINDER_MS = 30000;
  AnnouncementMode selectedMode = AnnouncementMode::Repeat;
  int32_t lastCompleted = 0;
  uint32_t lastCompletedAt = 0;
  bool hasCompleted = false;
  bool requested = false;
};

#endif
