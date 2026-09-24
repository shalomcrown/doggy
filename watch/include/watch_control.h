#ifndef DOGGY_WATCH_CONTROL_H
#define DOGGY_WATCH_CONTROL_H

#include <cstddef>

// ================================================================================

void watch_control_begin();

// ================================================================================

void watch_control_set_screen_active(bool active);

// ================================================================================

bool watch_control_screen_active();

// ================================================================================

bool watch_control_prevents_sleep();

// ================================================================================

void watch_control_service(bool wifi_connected, unsigned long now_ms);

// ================================================================================

void watch_control_stick(float normalized_x, float normalized_y, bool released);

// ================================================================================

void watch_control_refresh_message();

// ================================================================================

void watch_control_message(char *buffer, std::size_t length);

#endif
