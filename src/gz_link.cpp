#include "sim_plant.h"
#include "sim_video.h"

#include <plog/Log.h>

#include <gz/msgs/image.pb.h>
#include <gz/msgs/imu.pb.h>
#include <gz/msgs/odometry.pb.h>
#include <gz/msgs/twist.pb.h>
#include <gz/transport/Node.hh>

#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <random>
#include <string>
#include <thread>

#include <unistd.h>

namespace {

constexpr char kCmdTopic[] = "/model/doggy_rover/cmd_vel";
constexpr char kOdomTopic[] = "/model/doggy_rover/odometry";
constexpr char kImuTopic[] = "/imu";
constexpr char kLeftTopic[] = "/left_camera";
constexpr char kRightTopic[] = "/right_camera";

// ================================================================================

struct PoseSample {
    bool have = false;
    double x_m = 0.0;
    double y_m = 0.0;
    double z_m = 0.0;
    double yaw_rad = 0.0;
};

// ================================================================================

struct ImuSample {
    bool have = false;
    double ax = 0.0;
    double ay = 0.0;
    double az = 0.0;
    double gx = 0.0;
    double gy = 0.0;
    double gz = 0.0;
};

// ================================================================================

double yaw_from_quaternion(double x, double y, double z, double w) {
    return std::atan2(
            2.0 * (w * z + x * y),
            1.0 - 2.0 * (y * y + z * z));
}

// ================================================================================

std::string mediamtx_binary() {
    const char *env = std::getenv("DOGGY_MEDIAMTX_BIN");
    if (env != nullptr && env[0] != '\0') {
        if (std::strchr(env, '\n') != nullptr || ::access(env, X_OK) != 0) {
            return {};
        }
        return env;
    }
    const char *home = std::getenv("HOME");
    if (home != nullptr && home[0] != '\0') {
        const std::string home_bin = std::string(home) + "/.local/lib/doggy/mediamtx";
        if (::access(home_bin.c_str(), X_OK) == 0) {
            return home_bin;
        }
    }
    if (::access("/usr/lib/doggy/mediamtx", X_OK) == 0) {
        return "/usr/lib/doggy/mediamtx";
    }
    return {};
}

// ================================================================================

std::string mediamtx_config_path() {
#ifdef DOGGY_SIM_SOURCE_DIR
    return std::string(DOGGY_SIM_SOURCE_DIR) + "/sim/mediamtx.yml";
#else
    return "sim/mediamtx.yml";
#endif
}

// ================================================================================

class GazeboLink : public SimPlant {
public:
    explicit GazeboLink(const SimSettings &settings) : settings_(settings) {
    }

    // ================================================================================

    ~GazeboLink() override {
        stop();
    }

    // ================================================================================

    void start() override {
        if (started_ || settings_.spawn == false) {
            return;
        }
        const std::string mediamtx = mediamtx_binary();
        const std::string config = mediamtx_config_path();
        if (mediamtx.empty()) {
            PLOG_WARNING << "MediaMTX not found. Simulator cameras stay off. "
                            "Run ./install-prereqs.sh --mode simulator";
        } else if (video_.start(mediamtx, config) == false) {
            PLOG_WARNING << "simulator video did not start";
        } else {
            video_ok_.store(true);
        }
        cmd_pub_ = node_.Advertise<gz::msgs::Twist>(kCmdTopic);
        if (cmd_pub_.Valid() == false) {
            PLOG_WARNING << "could not advertise " << kCmdTopic;
        }
        if (node_.Subscribe(kOdomTopic, &GazeboLink::on_odom, this) == false) {
            PLOG_WARNING << "could not subscribe to " << kOdomTopic;
        }
        if (node_.Subscribe(kImuTopic, &GazeboLink::on_imu, this) == false) {
            PLOG_WARNING << "could not subscribe to " << kImuTopic;
        }
        if (node_.Subscribe(kLeftTopic, &GazeboLink::on_left, this) == false) {
            PLOG_WARNING << "could not subscribe to " << kLeftTopic;
        }
        if (node_.Subscribe(kRightTopic, &GazeboLink::on_right, this) == false) {
            PLOG_WARNING << "could not subscribe to " << kRightTopic;
        }
        started_ = true;
        command_thread_ = std::thread(&GazeboLink::command_loop, this);
    }

    // ================================================================================

    void stop() override {
        if (started_ == false) {
            video_.stop();
            return;
        }
        started_ = false;
        stop_.store(true);
        command_cv_.notify_all();
        if (command_thread_.joinable()) {
            command_thread_.join();
        }
        video_.stop();
        video_ok_.store(false);
    }

    // ================================================================================

    void set_command(const SimCommand &command) override {
        std::lock_guard<std::mutex> lock(twist_mutex_);
        linear_m_s_ = command.linear_m_s;
        yaw_rad_s_ = command.yaw_rad_s;
        last_command_ = std::chrono::steady_clock::now();
        have_command_ = true;
    }

    // ================================================================================

    bool latest_state(SimState &state) const override {
        PoseSample pose;
        ImuSample imu;
        double noise_east = 0.0;
        double noise_north = 0.0;
        {
            std::lock_guard<std::mutex> lock(pose_mutex_);
            pose = pose_;
            imu = imu_;
            if (pose.have) {
                double dt_s = 0.0;
                const auto now = std::chrono::steady_clock::now();
                if (gps_clock_started_) {
                    dt_s = std::chrono::duration<double>(now - gps_clock_).count();
                }
                gps_clock_ = now;
                gps_clock_started_ = true;
                std::uniform_real_distribution<double> unit(-1.0, 1.0);
                sim_step_gps_error(
                        gps_error_,
                        dt_s,
                        unit(rng_),
                        unit(rng_),
                        unit(rng_),
                        unit(rng_));
                noise_east = gps_error_.fast_east_m + gps_error_.slow_east_m;
                noise_north = gps_error_.fast_north_m + gps_error_.slow_north_m;
            }
        }
        state = SimState{};
        state.ok = pose.have;
        if (pose.have == false) {
            return false;
        }
        state.x_m = pose.x_m;
        state.y_m = pose.y_m;
        state.yaw_rad = pose.yaw_rad;
        state.altitude_m = settings_.altitude_m + pose.z_m;
        state.accuracy_m = kSimGpsSlowBoundM;
        const SimGpsFix fix = sim_project_gps(
                settings_.origin_latitude_deg,
                settings_.origin_longitude_deg,
                pose.x_m,
                pose.y_m,
                noise_east,
                noise_north);
        state.latitude_deg = fix.latitude_deg;
        state.longitude_deg = fix.longitude_deg;
        if (imu.have) {
            state.ax = imu.ax;
            state.ay = imu.ay;
            state.az = imu.az;
            state.gx = imu.gx;
            state.gy = imu.gy;
            state.gz = imu.gz;
        } else {
            state.az = 9.81;
        }
        return true;
    }

    // ================================================================================

    std::vector<SimCameraOffer> cameras() const override {
        std::vector<SimCameraOffer> offers;
        if (video_ok_.load() == false) {
            return offers;
        }
        for (const SimVideoStream &stream : video_.offers()) {
            SimCameraOffer offer;
            offer.id = stream.id;
            offer.name = stream.name;
            offer.webrtc_url = stream.webrtc_url;
            offers.push_back(std::move(offer));
        }
        return offers;
    }

    // ================================================================================

    void on_odom(const gz::msgs::Odometry &message) {
        const auto &position = message.pose().position();
        const auto &orientation = message.pose().orientation();
        std::lock_guard<std::mutex> lock(pose_mutex_);
        pose_.have = true;
        pose_.x_m = position.x();
        pose_.y_m = position.y();
        pose_.z_m = position.z();
        pose_.yaw_rad = yaw_from_quaternion(
                orientation.x(), orientation.y(), orientation.z(), orientation.w());
    }

    // ================================================================================

    void on_imu(const gz::msgs::IMU &message) {
        std::lock_guard<std::mutex> lock(pose_mutex_);
        imu_.have = true;
        imu_.ax = message.linear_acceleration().x();
        imu_.ay = message.linear_acceleration().y();
        imu_.az = message.linear_acceleration().z();
        imu_.gx = message.angular_velocity().x();
        imu_.gy = message.angular_velocity().y();
        imu_.gz = message.angular_velocity().z();
    }

    // ================================================================================

    void on_left(const gz::msgs::Image &message) {
        store_image(message, 0);
    }

    // ================================================================================

    void on_right(const gz::msgs::Image &message) {
        store_image(message, 1);
    }

private:
    // ================================================================================

    bool store_image(const gz::msgs::Image &message, int camera) {
        if (video_ok_.load() == false) {
            return false;
        }
        if (message.pixel_format_type() != gz::msgs::PixelFormatType::RGB_INT8) {
            return false;
        }
        const std::uint32_t width = message.width();
        const std::uint32_t height = message.height();
        if (width != static_cast<std::uint32_t>(kSimVideoWidth)
                || height != static_cast<std::uint32_t>(kSimVideoHeight)) {
            return false;
        }
        const std::uint64_t row = static_cast<std::uint64_t>(width) * 3ull;
        const std::uint64_t need = row * height;
        if (message.data().size() < need) {
            return false;
        }
        std::uint32_t step = message.step();
        if (step == 0) {
            step = static_cast<std::uint32_t>(row);
        }
        if (step < row) {
            return false;
        }
        std::vector<std::uint8_t> rgb(static_cast<std::size_t>(need));
        const std::string &data = message.data();
        for (std::uint32_t y = 0; y < height; ++y) {
            const std::size_t src = static_cast<std::size_t>(y) * step;
            if (src + row > data.size()) {
                return false;
            }
            std::memcpy(
                    rgb.data() + static_cast<std::size_t>(y) * row,
                    data.data() + src,
                    static_cast<std::size_t>(row));
        }
        video_.submit(camera, std::move(rgb), width, height);
        return true;
    }

    // ================================================================================

    void publish_twist(double linear_m_s, double yaw_rad_s) {
        if (cmd_pub_.Valid() == false) {
            return;
        }
        gz::msgs::Twist twist;
        twist.mutable_linear()->set_x(linear_m_s);
        twist.mutable_angular()->set_z(yaw_rad_s);
        cmd_pub_.Publish(twist);
    }

    // ================================================================================

    void command_loop() {
        while (stop_.load() == false) {
            double linear = 0.0;
            double yaw = 0.0;
            {
                std::unique_lock<std::mutex> lock(twist_mutex_);
                command_cv_.wait_for(lock, std::chrono::milliseconds(50));
                const auto now = std::chrono::steady_clock::now();
                const bool fresh = have_command_
                        && now - last_command_ <= std::chrono::milliseconds(500);
                if (fresh) {
                    linear = linear_m_s_;
                    yaw = yaw_rad_s_;
                }
            }
            if (stop_.load()) {
                break;
            }
            publish_twist(linear, yaw);
        }
        publish_twist(0.0, 0.0);
    }

    SimSettings settings_;
    gz::transport::Node node_;
    gz::transport::Node::Publisher cmd_pub_;
    SimVideo video_;
    std::atomic<bool> video_ok_{false};
    bool started_ = false;
    mutable std::mutex pose_mutex_;
    PoseSample pose_;
    ImuSample imu_;
    mutable std::mt19937 rng_{1};
    mutable SimGpsError gps_error_;
    mutable std::chrono::steady_clock::time_point gps_clock_{};
    mutable bool gps_clock_started_ = false;
    std::mutex twist_mutex_;
    std::condition_variable command_cv_;
    double linear_m_s_ = 0.0;
    double yaw_rad_s_ = 0.0;
    bool have_command_ = false;
    std::chrono::steady_clock::time_point last_command_{};
    std::atomic<bool> stop_{false};
    std::thread command_thread_;
};

}  // namespace

// ================================================================================

std::unique_ptr<SimPlant> make_sim_plant(const SimSettings &settings) {
    return std::make_unique<GazeboLink>(settings);
}
