#ifndef DOGGY_WATCH_NETWORK_H
#define DOGGY_WATCH_NETWORK_H

#include <ctime>

// ================================================================================

void watch_network_begin();

// ================================================================================

void watch_network_service();

// ================================================================================

// Starts ESP-Touch v2 now instead of waiting for the saved networks to fail.
void watch_network_start_provisioning();

// ================================================================================

// True while ESP-Touch v2 is listening, which is also when the watch must stay
// awake with its radio up.
bool watch_network_provisioning_active();

// ================================================================================

// Drops the radio for sleep. Does nothing while provisioning is listening.
void watch_network_suspend();

// ================================================================================

// Brings the radio back and retries the saved networks, newest first.
void watch_network_resume();

// ================================================================================

const char *watch_network_status();

// ================================================================================

bool watch_network_time_valid();

// ================================================================================

std::time_t watch_network_utc_time();

// ================================================================================

bool watch_network_connected();

// ================================================================================

const char *watch_network_ssid();

// ================================================================================

int watch_network_rssi();

// ================================================================================

// Seconds since the last SNTP update, or -1 when the clock has never synced.
long watch_network_sync_age_seconds();

#endif
