#ifndef DOGGY_WATCH_BOARD_HAL_H
#define DOGGY_WATCH_BOARD_HAL_H

#include <ctime>

// ================================================================================

bool watch_board_begin();

// ================================================================================

void watch_board_service();

// ================================================================================

void watch_board_poll_ui();

// ================================================================================

// False when the board exposes no battery gauge; percent/charging stay untouched.
bool watch_board_battery(int &percent, bool &charging);

// ================================================================================

// Pixels of every screen edge a rounded panel mask can eat. 0 on square panels.
int watch_board_safe_inset();

// ================================================================================

// True when the board carries a battery-backed calendar chip. Boards without one
// keep time only while powered.
bool watch_board_has_rtc();

// ================================================================================

// Reads the calendar chip into utc. False when the board has no chip or the chip
// holds a time too early to be real; utc stays untouched.
bool watch_board_rtc_read(std::time_t &utc);

// ================================================================================

// Copies a trusted UTC time onto the calendar chip so it survives a power cycle.
bool watch_board_rtc_write(std::time_t utc);

// ================================================================================

// Blanks the panel and halts the CPU until the wearer touches the screen (or
// moves their wrist, where the board has a motion sensor), then restores the
// panel. Returns only after the wake, so the caller resumes in the same place.
// Returns without blanking anything when no wake line can be armed, since a
// watch that sleeps with no way back is worse than one that stays awake.
void watch_board_sleep();

#endif
