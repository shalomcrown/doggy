#include "time_offset.h"

#include <algorithm>

namespace {

inline constexpr long long kSecondsPerDay = 86400;
// Days from 0000-03-01 to 1970-01-01, the shift that moves a March-based year
// onto the Unix epoch.
inline constexpr long long kEpochShiftDays = 719468;
inline constexpr long long kDaysPerEra = 146097;

}

// ================================================================================

int watch_utc_offset_seconds(int hours, int minutes) {
    hours = std::clamp(
            hours,
            kWatchMinimumOffsetHours,
            kWatchMaximumOffsetHours);
    minutes = std::clamp(minutes, 0, 59);
    minutes = std::min(45, ((minutes + 7) / 15) * 15);
    const int direction = hours < 0 ? -1 : 1;
    return hours * 60 * 60 + direction * minutes * 60;
}

// ================================================================================

// Counts days by treating March as the first month, which puts the leap day at
// the end of the year and makes every era of 400 years identical.
std::time_t watch_utc_time_from_civil(const std::tm &civil) {
    const int month = civil.tm_mon + 1;
    const int year = civil.tm_year + 1900 - (month <= 2 ? 1 : 0);
    const long long era = (year >= 0 ? year : year - 399) / 400;
    const long long year_of_era = year - era * 400;
    const long long day_of_year =
            (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5
            + civil.tm_mday - 1;
    const long long day_of_era = year_of_era * 365
            + year_of_era / 4
            - year_of_era / 100
            + day_of_year;
    const long long days =
            era * kDaysPerEra + day_of_era - kEpochShiftDays;
    return static_cast<std::time_t>(
            days * kSecondsPerDay
            + civil.tm_hour * 3600
            + civil.tm_min * 60
            + civil.tm_sec);
}
