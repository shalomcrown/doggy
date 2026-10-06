#include "sim_plant.h"

// ================================================================================

namespace {

// ================================================================================

class StubPlant : public SimPlant {
public:
    // ================================================================================

    void start() override {
    }

    // ================================================================================

    void stop() override {
    }

    // ================================================================================

    void set_command(const SimCommand &) override {
    }

    // ================================================================================

    bool latest_state(SimState &) const override {
        return false;
    }

    // ================================================================================

    std::vector<SimCameraOffer> cameras() const override {
        return {};
    }
};

}  // namespace

// ================================================================================

std::unique_ptr<SimPlant> make_sim_plant(const SimSettings &) {
    return std::make_unique<StubPlant>();
}
