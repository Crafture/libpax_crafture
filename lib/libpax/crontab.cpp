#include "crontab.h"

bool    crontab(uint8_t target_hour, uint8_t target_minute) {
  uint8_t current_hour = static_cast<uint8_t>(hour());
  uint8_t current_minute = static_cast<uint8_t>(minute());

  if (current_hour == target_hour && current_minute == target_minute) {
    return true;
  }
  return false;
}