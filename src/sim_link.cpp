#include "sim_link.h"

#include "motor_math.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <fstream>
#include <iterator>

namespace {

constexpr double kMetresPerDegree = 111320.0;

// ================================================================================

bool finite_number(const nlohmann::json &value, double &out) {
    if (value.is_number() == false) {
        return false;
    }
    out = value.get<double>();
    return std::isfinite(out);
}

// ================================================================================

bool allowed_bind_host(const std::string &host) {
    return host == "127.0.0.1" || host == "localhost" || host == "0.0.0.0";
}

// ================================================================================

bool port_in_range(int port) {
    return port >= 1 && port <= 65535;
}

// ================================================================================

bool take_json_port(
        const nlohmann::json &parsed,
        const char *name,
        int &slot,
        std::string &error) {
    if (parsed.contains(name) == false) {
        return true;
    }
    if (parsed[name].is_number_integer() == false) {
        error = std::string(name) + " must be an integer port";
        return false;
    }
    slot = parsed[name].get<int>();
    return true;
}

// ================================================================================

bool take_json_real(
        const nlohmann::json &parsed,
        const char *name,
        double &slot,
        std::string &error) {
    if (parsed.contains(name) == false) {
        return true;
    }
    if (finite_number(parsed[name], slot) == false) {
        error = std::string(name) + " must be a finite number";
        return false;
    }
    return true;
}

}  // namespace

// ================================================================================

SimCommand sim_twist(
        double speed,
        double turn,
        double max_speed_m_s,
        double track_width_m,
        double turn_gain_min) {
    SimCommand command;
    if (std::isfinite(speed) == false || std::isfinite(turn) == false
            || std::isfinite(max_speed_m_s) == false || max_speed_m_s <= 0.0
            || std::isfinite(track_width_m) == false || track_width_m <= 0.0) {
        return command;
    }
    const ArcadeMix mix = mix_arcade(speed, turn, turn_gain_min);
    const double left = mix.left * max_speed_m_s;
    const double right = mix.right * max_speed_m_s;
    command.linear_m_s = (left + right) / 2.0;
    command.yaw_rad_s = (right - left) / track_width_m;
    return command;
}

// ================================================================================

SimGpsFix sim_project_gps(
        double origin_latitude_deg,
        double origin_longitude_deg,
        double east_m,
        double north_m,
        double noise_east_m,
        double noise_north_m) {
    SimGpsFix fix;
    fix.latitude_deg = origin_latitude_deg;
    fix.longitude_deg = origin_longitude_deg;
    if (std::isfinite(origin_latitude_deg) == false
            || std::isfinite(origin_longitude_deg) == false
            || std::isfinite(east_m) == false
            || std::isfinite(north_m) == false
            || std::isfinite(noise_east_m) == false
            || std::isfinite(noise_north_m) == false) {
        return fix;
    }
    const double lat_rad = origin_latitude_deg * M_PI / 180.0;
    double cos_lat = std::cos(lat_rad);
    if (std::abs(cos_lat) < 1e-6) {
        cos_lat = cos_lat < 0.0 ? -1e-6 : 1e-6;
    }
    fix.latitude_deg = origin_latitude_deg
            + (north_m + noise_north_m) / kMetresPerDegree;
    fix.longitude_deg = origin_longitude_deg
            + (east_m + noise_east_m) / (kMetresPerDegree * cos_lat);
    return fix;
}

// ================================================================================

bool sim_load_settings(
        const std::string &path,
        SimSettings &settings,
        std::string &error) {
    settings = SimSettings{};
    error.clear();
    if (path.empty()) {
        return true;
    }
    std::ifstream file(path);
    if (file.is_open() == false) {
        return true;
    }
    const std::string text(
            (std::istreambuf_iterator<char>(file)),
            std::istreambuf_iterator<char>());
    if (file.bad()) {
        error = "could not read simulator settings";
        return false;
    }
    nlohmann::json parsed;
    try {
        parsed = nlohmann::json::parse(text);
    } catch (const nlohmann::json::exception &) {
        error = "simulator settings are not JSON";
        return false;
    }
    if (parsed.is_object() == false) {
        error = "simulator settings are not a JSON object";
        return false;
    }
    SimSettings next;
    if (parsed.contains("bind_host")) {
        if (parsed["bind_host"].is_string() == false) {
            error = "bind_host must be a string";
            return false;
        }
        next.bind_host = parsed["bind_host"].get<std::string>();
    }
    if (take_json_port(parsed, "http_port", next.http_port, error) == false
            || take_json_real(parsed, "max_speed_m_s", next.max_speed_m_s, error) == false
            || take_json_real(parsed, "track_width_m", next.track_width_m, error) == false
            || take_json_real(parsed, "origin_latitude_deg", next.origin_latitude_deg, error) == false
            || take_json_real(parsed, "origin_longitude_deg", next.origin_longitude_deg, error) == false
            || take_json_real(parsed, "altitude_m", next.altitude_m, error) == false
            || take_json_real(parsed, "gps_sigma_m", next.gps_sigma_m, error) == false) {
        return false;
    }
    if (parsed.contains("spawn")) {
        if (parsed["spawn"].is_boolean() == false) {
            error = "spawn must be a boolean";
            return false;
        }
        next.spawn = parsed["spawn"].get<bool>();
    }
    if (allowed_bind_host(next.bind_host) == false) {
        error = "bind_host must be 127.0.0.1, localhost, or 0.0.0.0";
        return false;
    }
    if (port_in_range(next.http_port) == false) {
        error = "http_port must be from 1 to 65535";
        return false;
    }
    if (next.max_speed_m_s <= 0.0 || next.max_speed_m_s > 20.0) {
        error = "max_speed_m_s must be from just above 0 through 20";
        return false;
    }
    if (next.track_width_m <= 0.0 || next.track_width_m > 5.0) {
        error = "track_width_m must be from just above 0 through 5";
        return false;
    }
    if (next.origin_latitude_deg < -90.0 || next.origin_latitude_deg > 90.0
            || next.origin_longitude_deg < -180.0 || next.origin_longitude_deg > 180.0) {
        error = "origin latitude or longitude is out of range";
        return false;
    }
    if (next.altitude_m < -500.0 || next.altitude_m > 10000.0) {
        error = "altitude_m is out of range";
        return false;
    }
    if (next.gps_sigma_m < 0.0 || next.gps_sigma_m > 100.0) {
        error = "gps_sigma_m must be from 0 through 100";
        return false;
    }
    settings = next;
    return true;
}

// ================================================================================

namespace {

// ================================================================================

double clamp_unit(double value) {
    if (std::isfinite(value) == false) {
        return 0.0;
    }
    if (value > 1.0) {
        return 1.0;
    }
    if (value < -1.0) {
        return -1.0;
    }
    return value;
}

// ================================================================================

void apply_horizontal_bound(
        double &east_m,
        double &north_m,
        double east_unit,
        double north_unit,
        double bound_m) {
    double east = clamp_unit(east_unit);
    double north = clamp_unit(north_unit);
    const double radius = std::hypot(east, north);
    if (radius > 1.0) {
        east /= radius;
        north /= radius;
    }
    east_m = east * bound_m;
    north_m = north * bound_m;
}

}  // namespace

// ================================================================================

SimGpsErrorStep sim_step_gps_error(
        SimGpsError &error,
        double dt_s,
        double fast_east_unit,
        double fast_north_unit,
        double slow_east_unit,
        double slow_north_unit) {
    SimGpsErrorStep step;
    if (std::isfinite(dt_s) == false || dt_s < 0.0) {
        dt_s = 0.0;
    }
    if (error.primed == false) {
        error.fast_elapsed_s = kSimGpsFastPeriodS;
        error.slow_elapsed_s = kSimGpsSlowPeriodS;
        error.primed = true;
    } else {
        error.fast_elapsed_s += dt_s;
        error.slow_elapsed_s += dt_s;
    }
    if (error.fast_elapsed_s >= kSimGpsFastPeriodS) {
        apply_horizontal_bound(
                error.fast_east_m,
                error.fast_north_m,
                fast_east_unit,
                fast_north_unit,
                kSimGpsFastBoundM);
        error.fast_elapsed_s = 0.0;
        step.refresh_fast = true;
    }
    if (error.slow_elapsed_s >= kSimGpsSlowPeriodS) {
        apply_horizontal_bound(
                error.slow_east_m,
                error.slow_north_m,
                slow_east_unit,
                slow_north_unit,
                kSimGpsSlowBoundM);
        error.slow_elapsed_s = 0.0;
        step.refresh_slow = true;
    }
    return step;
}
