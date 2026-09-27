#include "stereo_bindings.h"

#include <cstdlib>
#include <iostream>

static int failures = 0;

// ================================================================================

static void expect(bool condition, const char *name) {
    if (condition) {
        std::cout << "PASS " << name << std::endl;
        return;
    }
    std::cout << "FAIL " << name << std::endl;
    failures += 1;
}

// ================================================================================

int main() {
    Config config;
    StereoBindings bindings;
    expect(stereo_bindings_from_config(config).left_stable_id.empty(),
           "missing camera_pair section loads empty");

    bindings.left_stable_id = "platform/usb-1";
    bindings.right_stable_id = "platform/usb-2";
    stereo_bindings_apply(&config, bindings);
    expect(stereo_bindings_from_config(config).left_stable_id == "platform/usb-1"
                    && stereo_bindings_from_config(config).right_stable_id
                            == "platform/usb-2",
           "apply writes camera_pair into config");

    stereo_bindings_swap(&bindings);
    expect(bindings.left_stable_id == "platform/usb-2"
                    && bindings.right_stable_id == "platform/usb-1",
           "swap exchanges left and right");

    StereoBindings defaults;
    expect(stereo_bindings_ensure_defaults(
                    &defaults,
                    {"z-id", "a-id", "m-id"}),
           "ensure defaults reports change");
    expect(defaults.left_stable_id == "a-id" && defaults.right_stable_id == "m-id",
           "ensure defaults picks two sorted stable ids");

    expect(stereo_bindings_stable_id_ok("../bad") == false,
           "stable id rejects parent traversal");

    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
