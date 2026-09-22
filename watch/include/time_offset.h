#ifndef DOGGY_WATCH_TIME_OFFSET_H
#define DOGGY_WATCH_TIME_OFFSET_H

#include <ctime>

// 2024-01-01 UTC. Anything earlier means the clock was never set, so both the
// NTP path and the hardware RTC treat it as "no time yet".
inline constexpr std::time_t kWatchMinimumValidTime = 1704067200;

inline constexpr int kWatchDefaultOffsetHours = 3;
inline constexpr int kWatchDefaultOffsetMinutes = 0;
inline constexpr int kWatchMinimumOffsetHours = -12;
inline constexpr int kWatchMaximumOffsetHours = 14;

// ================================================================================

int watch_utc_offset_seconds(int hours, int minutes);

// ================================================================================

// The inverse of gmtime_r. The standard timegm is missing from the ESP32 C
// library, and mktime would fold in whatever local zone the firmware is set to,
// so the calendar-to-epoch direction is done here instead.
std::time_t watch_utc_time_from_civil(const std::tm &civil);

#endif
