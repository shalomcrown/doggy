#ifndef STEREO_BINDINGS_H
#define STEREO_BINDINGS_H

#include "config.h"

#include <string>
#include <vector>

// ================================================================================

struct StereoBindings {
    std::string left_stable_id;
    std::string right_stable_id;
};

// ================================================================================

StereoBindings stereo_bindings_from_config(const Config &config);

// ================================================================================

void stereo_bindings_apply(Config *config, const StereoBindings &bindings);

// ================================================================================

void stereo_bindings_swap(StereoBindings *bindings);

// ================================================================================

bool stereo_bindings_stable_id_ok(const std::string &stable_id);

// ================================================================================

bool stereo_bindings_assign(
        StereoBindings *bindings,
        const std::string &left,
        const std::string &right);

// ================================================================================

bool stereo_bindings_ensure_defaults(
        StereoBindings *bindings,
        const std::vector<std::string> &detected_stable_ids);

#endif
