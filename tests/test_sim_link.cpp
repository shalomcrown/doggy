#include "sim_link.h"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

static int failures = 0;

// ================================================================================

static void expect(bool cond, const char *name) {
    if (cond) {
        std::cout << "PASS " << name << std::endl;
        return;
    }
    std::cout << "FAIL " << name << std::endl;
    failures += 1;
}

// ================================================================================

static bool near(double a, double b) {
    return std::abs(a - b) < 1e-6;
}

// ================================================================================

int main() {
    const SimCommand straight = sim_twist(1.0, 0.0, 2.0, 0.20, 1.0);
    expect(near(straight.linear_m_s, 2.0) && near(straight.yaw_rad_s, 0.0),
           "full stick ahead is 2 m/s with no yaw");

    const SimCommand spin = sim_twist(0.0, 1.0, 2.0, 0.20, 1.0);
    expect(near(spin.linear_m_s, 0.0) && near(spin.yaw_rad_s, -20.0),
           "full stick turn yaws clockwise as negative Gazebo yaw");

    const SimCommand bad = sim_twist(1.0, 0.0, 2.0, 0.0, 1.0);
    expect(bad.linear_m_s == 0.0 && bad.yaw_rad_s == 0.0,
           "a zero track width produces a zero command");

    const SimGpsFix north = sim_project_gps(10.0, 20.0, 0.0, 111320.0, 0.0, 0.0);
    expect(near(north.latitude_deg, 11.0) && near(north.longitude_deg, 20.0),
           "111320 m north is one degree of latitude");

    const double east = 111320.0 * std::cos(10.0 * M_PI / 180.0);
    const SimGpsFix moved = sim_project_gps(10.0, 20.0, east, 0.0, 0.0, 0.0);
    expect(near(moved.longitude_deg, 21.0),
           "one degree of longitude projects back to the origin meridian offset");

    const char *dir = std::getenv("TMPDIR");
    const std::string folder = dir == nullptr ? "/tmp" : dir;
    const std::string missing = folder + "/doggy-sim-settings-missing.json";
    SimSettings settings;
    std::string error;
    expect(sim_load_settings(missing, settings, error)
                    && settings.http_port == 8080
                    && near(settings.max_speed_m_s, 2.0)
                    && settings.bind_host == "127.0.0.1"
                    && settings.spawn,
           "a missing settings file uses the built-in defaults");

    const std::string bad_path = folder + "/doggy-sim-settings-bad.json";
    {
        std::ofstream out(bad_path);
        out << "{\"bind_host\":\"10.0.0.8\"}\n";
    }
    expect(sim_load_settings(bad_path, settings, error) == false,
           "a non-loopback bind host is rejected");
    std::remove(bad_path.c_str());

    const std::string old_path = folder + "/doggy-sim-settings-old.json";
    {
        std::ofstream out(old_path);
        out << "{\"link_port\":8090,\"image_port\":8091,\"http_port\":8080}\n";
    }
    expect(sim_load_settings(old_path, settings, error)
                    && settings.http_port == 8080,
           "retired link ports in an old settings file are ignored");
    std::remove(old_path.c_str());

    SimGpsError gps_error;
    const SimGpsErrorStep primed = sim_step_gps_error(gps_error, 0.0, 1.0, 0.0, 1.0, 0.0);
    expect(primed.refresh_fast && primed.refresh_slow
                    && near(gps_error.fast_east_m, 0.2)
                    && near(gps_error.fast_north_m, 0.0)
                    && near(gps_error.slow_east_m, 2.5)
                    && near(gps_error.slow_north_m, 0.0),
           "the first sample is 0.2 m at 1 Hz and 2.5 m at 0.01 Hz");

    const SimGpsErrorStep held = sim_step_gps_error(gps_error, 0.5, -1.0, -1.0, -1.0, -1.0);
    expect(held.refresh_fast == false && held.refresh_slow == false
                    && near(gps_error.fast_east_m, 0.2)
                    && near(gps_error.slow_east_m, 2.5),
           "noise holds between the 1 Hz and 0.01 Hz updates");

    const SimGpsErrorStep fast = sim_step_gps_error(gps_error, 0.5, 0.0, 1.0, -1.0, 0.0);
    expect(fast.refresh_fast && fast.refresh_slow == false
                    && near(gps_error.fast_east_m, 0.0)
                    && near(gps_error.fast_north_m, 0.2)
                    && near(gps_error.slow_east_m, 2.5),
           "the fast term updates at 1 Hz and the slow term stays");

    SimGpsError diagonal;
    sim_step_gps_error(diagonal, 0.0, 1.0, 1.0, 1.0, 1.0);
    expect(std::hypot(diagonal.fast_east_m, diagonal.fast_north_m) <= 0.2 + 1e-9
                    && std::hypot(diagonal.slow_east_m, diagonal.slow_north_m) <= 2.5 + 1e-9,
           "horizontal error stays inside the 0.2 m and 2.5 m bounds");

    SimGpsError later = gps_error;
    const SimGpsErrorStep slow = sim_step_gps_error(later, 100.0, 0.0, 0.0, 0.0, -1.0);
    expect(slow.refresh_slow && near(later.slow_north_m, -2.5),
           "the slow term updates at 0.01 Hz");

    return failures == 0 ? 0 : 1;
}
