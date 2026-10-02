#include "stereo_bindings.h"

#include <algorithm>
#include <cctype>
#include <set>

// ================================================================================

StereoBindings stereo_bindings_from_config(const Config &config) {
    StereoBindings bindings;
    if (config.has_camera_pair()) {
        bindings.left_stable_id = config.camera_pair().left_stable_id();
        bindings.right_stable_id = config.camera_pair().right_stable_id();
    }

    if (stereo_bindings_stable_id_ok(bindings.left_stable_id) == false) {
        bindings.left_stable_id.clear();
    }
    if (stereo_bindings_stable_id_ok(bindings.right_stable_id) == false) {
        bindings.right_stable_id.clear();
    }

    return bindings;
}

// ================================================================================

void stereo_bindings_apply(Config *config, const StereoBindings &bindings) {
    if (config == nullptr) {
        return;
    }

    doggy::v1::CameraPair *pair = config->mutable_camera_pair();
    pair->set_left_stable_id(bindings.left_stable_id);
    pair->set_right_stable_id(bindings.right_stable_id);
}

// ================================================================================

bool stereo_bindings_stable_id_ok(const std::string &stable_id) {
    if (stable_id.empty() || stable_id.size() > 256) {
        return false;
    }

    if (stable_id.find("..") != std::string::npos) {
        return false;
    }

    for (char ch : stable_id) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (std::isalnum(c) != 0 || ch == '/' || ch == '-' || ch == '_' || ch == '.'
                || ch == ':') {
            continue;
        }

        return false;
    }

    return true;
}

// ================================================================================

void stereo_bindings_swap(StereoBindings *bindings) {
    if (bindings == nullptr) {
        return;
    }

    std::swap(bindings->left_stable_id, bindings->right_stable_id);
}

// ================================================================================

bool stereo_bindings_assign(
        StereoBindings *bindings,
        const std::string &left,
        const std::string &right) {
    if (bindings == nullptr || left.empty() || right.empty() || left == right) {
        return false;
    }

    if (stereo_bindings_stable_id_ok(left) == false
            || stereo_bindings_stable_id_ok(right) == false) {
        return false;
    }

    bindings->left_stable_id = left;
    bindings->right_stable_id = right;
    return true;
}

// ================================================================================

bool stereo_bindings_ensure_defaults(
        StereoBindings *bindings,
        const std::vector<std::string> &detected_stable_ids) {
    if (bindings == nullptr) {
        return false;
    }

    const StereoBindings before = *bindings;
    std::vector<std::string> sorted;
    std::set<std::string> unique;
    for (const std::string &id : detected_stable_ids) {
        if (stereo_bindings_stable_id_ok(id)) {
            unique.insert(id);
        }
    }

    sorted.assign(unique.begin(), unique.end());
    if (sorted.size() < 2) {
        return false;
    }

    if (bindings->left_stable_id.empty() && bindings->right_stable_id.empty()) {
        bindings->left_stable_id = sorted[0];
        bindings->right_stable_id = sorted[1];
        return before.left_stable_id != bindings->left_stable_id
                || before.right_stable_id != bindings->right_stable_id;
    }

    if (bindings->left_stable_id.empty() == false
            && bindings->right_stable_id.empty() == false) {
        return false;
    }

    const std::string existing = bindings->left_stable_id.empty()
            ? bindings->right_stable_id
            : bindings->left_stable_id;
    for (const std::string &candidate : sorted) {
        if (candidate == existing) {
            continue;
        }

        if (bindings->left_stable_id.empty()) {
            bindings->left_stable_id = candidate;
        } else {
            bindings->right_stable_id = candidate;
        }

        break;
    }

    return before.left_stable_id != bindings->left_stable_id
            || before.right_stable_id != bindings->right_stable_id;
}
