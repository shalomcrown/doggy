#ifndef DOGGY_WATCH_ROVER_WORKER_H
#define DOGGY_WATCH_ROVER_WORKER_H

#include <cstddef>
#include <cstdint>

// ================================================================================

void watch_rover_worker_begin();

// ================================================================================

void watch_rover_worker_set_wifi_connected(bool connected);

// ================================================================================

void watch_rover_worker_set_screen_active(bool active);

// ================================================================================

void watch_rover_worker_set_drive_command(float speed, float turn, bool stick_active);

// ================================================================================

void watch_rover_worker_request_stop();

// ================================================================================

void watch_rover_worker_refresh_selection_message();

// ================================================================================

void watch_rover_worker_message(char *buffer, std::size_t length);

// ================================================================================

std::uint32_t watch_rover_worker_message_revision();

#endif
