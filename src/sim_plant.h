#ifndef SIM_PLANT_H
#define SIM_PLANT_H

#include "sim_link.h"

#include <memory>
#include <string>
#include <vector>

// ================================================================================

struct SimCameraOffer {
    std::string id;
    std::string name;
    std::string webrtc_url;
};

// ================================================================================

class SimPlant {
public:
    virtual ~SimPlant() = default;
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual void set_command(const SimCommand &command) = 0;
    virtual bool latest_state(SimState &state) const = 0;
    virtual std::vector<SimCameraOffer> cameras() const = 0;
};

// ================================================================================

std::unique_ptr<SimPlant> make_sim_plant(const SimSettings &settings);

#endif
