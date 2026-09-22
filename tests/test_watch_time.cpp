#include "time_offset.h"

#include <cstdlib>
#include <ctime>
#include <iostream>

static int failures = 0;

// ================================================================================

static void expect(bool condition, const char *name) {
    if (condition) {
        return;
    }
    std::cerr << "FAIL " << name << std::endl;
    failures += 1;
}

// ================================================================================

// The watch reads its calendar chip through this conversion, so it has to be the
// exact inverse of gmtime_r across leap days, century years, and year ends.
static void test_civil_round_trip() {
    const std::time_t samples[] = {
            0,
            kWatchMinimumValidTime,
            951782400,   // 2000-02-29, a leap day in a century leap year
            1709164800,  // 2024-02-29
            1735689599,  // one second before 2025-01-01
            2145916800,  // 2038-01-01, past the 32-bit time_t rollover
    };
    for (const std::time_t sample : samples) {
        std::tm civil{};
        gmtime_r(&sample, &civil);
        expect(
                watch_utc_time_from_civil(civil) == sample,
                "civil time converts back to the same instant");
    }
}

// ================================================================================

int main() {
    test_civil_round_trip();
    expect(watch_utc_offset_seconds(3, 0) == 10800,
           "UTC+3:00 converts to seconds");
    expect(watch_utc_offset_seconds(-5, 30) == -19800,
           "UTC-5:30 keeps minutes negative");
    expect(watch_utc_offset_seconds(0, 15) == 900,
           "UTC+0:15 converts to seconds");
    expect(watch_utc_offset_seconds(14, 0) == 50400,
           "UTC+14:00 converts to seconds");
    expect(watch_utc_offset_seconds(-12, 0) == -43200,
           "UTC-12:00 converts to seconds");
    expect(watch_utc_offset_seconds(15, 0) == 50400,
           "hours clamp to positive boundary");
    expect(watch_utc_offset_seconds(-13, 0) == -43200,
           "hours clamp to negative boundary");
    expect(watch_utc_offset_seconds(3, 17) == 11700,
           "minutes quantize to a 15-minute interval");

    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
