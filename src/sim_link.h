#ifndef SIM_LINK_H
#define SIM_LINK_H

#include <string>

// Host-simulator math and settings. Gazebo I/O lives in the simulator process.
// These values are not part of doggy.json.

inline constexpr int kSimHttpPort = 8080;
inline constexpr double kSimMaxSpeedMps = 2.0;
inline constexpr double kSimTrackWidthM = 0.20;
inline constexpr double kSimOriginLatitudeDeg = 32.0783;
inline constexpr double kSimOriginLongitudeDeg = 34.8489;
inline constexpr double kSimAltitudeM = 60.0;
inline constexpr double kSimGpsSigmaM = 3.0;
inline constexpr double kSimGpsFastPeriodS = 1.0;
inline constexpr double kSimGpsFastBoundM = 0.2;
inline constexpr double kSimGpsSlowPeriodS = 100.0;
inline constexpr double kSimGpsSlowBoundM = 2.5;

// ================================================================================

struct SimCommand {
    double linear_m_s = 0.0;
    double yaw_rad_s = 0.0;
};

// ================================================================================

struct SimState {
    bool ok = false;
    double x_m = 0.0;
    double y_m = 0.0;
    double yaw_rad = 0.0;
    double latitude_deg = 0.0;
    double longitude_deg = 0.0;
    double altitude_m = 0.0;
    double accuracy_m = 0.0;
    double ax = 0.0;
    double ay = 0.0;
    double az = 0.0;
    double gx = 0.0;
    double gy = 0.0;
    double gz = 0.0;
};

// ================================================================================

struct SimGpsFix {
    double latitude_deg = 0.0;
    double longitude_deg = 0.0;
};

// ================================================================================

struct SimGpsError {
    double fast_east_m = 0.0;
    double fast_north_m = 0.0;
    double slow_east_m = 0.0;
    double slow_north_m = 0.0;
    double fast_elapsed_s = 0.0;
    double slow_elapsed_s = 0.0;
    bool primed = false;
};

// ================================================================================

struct SimGpsErrorStep {
    bool refresh_fast = false;
    bool refresh_slow = false;
};

// ================================================================================

struct SimSettings {
    std::string bind_host = "127.0.0.1";
    int http_port = kSimHttpPort;
    double max_speed_m_s = kSimMaxSpeedMps;
    double track_width_m = kSimTrackWidthM;
    double origin_latitude_deg = kSimOriginLatitudeDeg;
    double origin_longitude_deg = kSimOriginLongitudeDeg;
    double altitude_m = kSimAltitudeM;
    // Older settings files may still contain this. The published error is the
    // 1 Hz and 0.01 Hz bounds below, not this sigma.
    double gps_sigma_m = kSimGpsSigmaM;
    bool spawn = true;
};

// ================================================================================

SimCommand sim_twist(
        double speed,
        double turn,
        double max_speed_m_s,
        double track_width_m,
        double turn_gain_min);

// ================================================================================

SimGpsFix sim_project_gps(
        double origin_latitude_deg,
        double origin_longitude_deg,
        double east_m,
        double north_m,
        double noise_east_m,
        double noise_north_m);

// ================================================================================

SimGpsErrorStep sim_step_gps_error(
        SimGpsError &error,
        double dt_s,
        double fast_east_unit,
        double fast_north_unit,
        double slow_east_unit,
        double slow_north_unit);

// ================================================================================

bool sim_load_settings(
        const std::string &path,
        SimSettings &settings,
        std::string &error);

#endif
