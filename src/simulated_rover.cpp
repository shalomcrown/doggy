#include "simulated_rover.h"

#include <chrono>

// ================================================================================

SimulatedRover::SimulatedRover(
        const Config &config,
        std::string config_path,
        const SimSettings &settings) :
    Rover(config, std::move(config_path), std::make_unique<NullSystemControl>()),
    settings_(settings),
    plant_(make_sim_plant(settings)),
    pending_gain_(config.robot().turn_gain_min()) {
    plant_->start();
    publisher_ = std::thread(&SimulatedRover::publishLoop, this);
}

// ================================================================================

SimulatedRover::~SimulatedRover() {
    stop_.store(true);
    publish_cv_.notify_all();
    if (publisher_.joinable()) {
        publisher_.join();
    }
    plant_->stop();
}

// ================================================================================

void SimulatedRover::onCommandChangedUnlocked() {
    if (stop_.load()) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(publish_mutex_);
        pending_speed_ = commandedSpeedUnlocked();
        pending_turn_ = commandedTurnUnlocked();
        pending_gain_ = config_.robot().turn_gain_min();
    }
    publish_cv_.notify_one();
}

// ================================================================================

void SimulatedRover::publishLoop() {
    while (stop_.load() == false) {
        double speed = 0.0;
        double turn = 0.0;
        double gain = 1.0;
        {
            std::unique_lock<std::mutex> lock(publish_mutex_);
            publish_cv_.wait_for(lock, std::chrono::milliseconds(200));
            speed = pending_speed_;
            turn = pending_turn_;
            gain = pending_gain_;
        }
        if (stop_.load()) {
            break;
        }
        plant_->set_command(sim_twist(
                speed,
                turn,
                settings_.max_speed_m_s,
                settings_.track_width_m,
                gain));
    }
}

// ================================================================================

void SimulatedRover::poll() {
}

// ================================================================================

std::vector<SimCameraOffer> SimulatedRover::cameraOffers() const {
    if (plant_ == nullptr) {
        return {};
    }
    return plant_->cameras();
}

// ================================================================================

DogStatus SimulatedRover::getStatus() const {
    DogStatus copy = Rover::getStatus();
    for (int i = copy.errors_size() - 1; i >= 0; --i) {
        if (copy.errors(i).code() == doggy::v1::i2c) {
            copy.mutable_errors()->DeleteSubrange(i, 1);
        }
    }

    SimState state;
    const bool linked = plant_->latest_state(state) && state.ok;
    doggy::v1::Gps *gps = copy.mutable_gps();
    if (linked == false) {
        gps->set_ok(false);
        gps->set_fix_type(doggy::v1::GPS_FIX_NO_GPS);
        gps->set_spoofing(doggy::v1::GPS_SPOOF_UNKNOWN);
        gps->set_jamming(doggy::v1::GPS_JAM_UNKNOWN);
        doggy::v1::Error *error = copy.add_errors();
        error->set_code(doggy::v1::gps);
        error->set_message("simulator link down");
        return copy;
    }

    const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    gps->set_ok(true);
    gps->set_fix_unix_ms(now_ms);
    gps->set_latitude_deg(state.latitude_deg);
    gps->set_longitude_deg(state.longitude_deg);
    gps->set_altitude_amsl_m(state.altitude_m);
    gps->set_satellites(12);
    gps->set_fix_type(doggy::v1::GPS_FIX_3D);
    gps->set_accuracy_m(state.accuracy_m);
    gps->set_spoofing(doggy::v1::GPS_SPOOF_NONE);
    gps->set_jamming(doggy::v1::GPS_JAM_OK);

    doggy::v1::Imu *imu = copy.mutable_imu();
    imu->set_ok(true);
    imu->mutable_accel()->set_x(state.ax);
    imu->mutable_accel()->set_y(state.ay);
    imu->mutable_accel()->set_z(state.az);
    imu->mutable_gyro()->set_x(state.gx);
    imu->mutable_gyro()->set_y(state.gy);
    imu->mutable_gyro()->set_z(state.gz);

    doggy::v1::Battery *battery = copy.mutable_battery();
    battery->set_ok(true);
    battery->set_voltage_v(8.4);
    return copy;
}
