#ifndef SIMULATED_ROVER_H
#define SIMULATED_ROVER_H

#include "rover.h"
#include "sim_plant.h"

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

// ================================================================================

class SimulatedRover : public Rover {
public:
    SimulatedRover(
            const Config &config,
            std::string config_path,
            const SimSettings &settings);
    ~SimulatedRover() override;

    DogStatus getStatus() const override;
    void poll();
    std::vector<SimCameraOffer> cameraOffers() const;

protected:
    void onCommandChangedUnlocked() override;

private:
    void publishLoop();

    SimSettings settings_;
    std::unique_ptr<SimPlant> plant_;
    std::mutex publish_mutex_;
    std::condition_variable publish_cv_;
    double pending_speed_ = 0.0;
    double pending_turn_ = 0.0;
    double pending_gain_ = 1.0;
    std::atomic<bool> stop_{false};
    std::thread publisher_;
};

#endif
