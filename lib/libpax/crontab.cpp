#include "crontab.h"
#include <iostream>

bool    crontab(uint8_t target_hour, uint8_t target_minute) {
  time_t current_time = time(NULL);

  // Convert the current time to a struct tm
  struct tm *local_time = localtime(&current_time);
  if (local_time->tm_hour == target_hour && local_time->tm_min == target_minute) {
    std::cout << "CRONTAB True" << std::endl;
    return (true);
  }
  std::cout << "CURRENT TIME " << local_time->tm_hour << ":" << local_time->tm_min << std::endl;
  return (false);
}
