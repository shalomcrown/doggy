#ifndef CAMERA_IDS_H
#define CAMERA_IDS_H

#include <string>

// ================================================================================

inline constexpr const char *kCameraPrimaryId = "cam0";
inline constexpr const char *kCameraUsbPairLeftId = "usb_left";
inline constexpr const char *kCameraUsbPairRightId = "usb_right";

// Legacy stream ids (single dual-lens stereo UVC device naming).
inline constexpr const char *kCameraLegacyPairLeftId = "stereo_left";
inline constexpr const char *kCameraLegacyPairRightId = "stereo_right";

// ================================================================================

inline bool camera_id_is_usb_pair(const std::string &id) {
    return id == kCameraUsbPairLeftId || id == kCameraUsbPairRightId
            || id == kCameraLegacyPairLeftId || id == kCameraLegacyPairRightId;
}

// ================================================================================

inline bool camera_id_is_usb_pair_left(const std::string &id) {
    return id == kCameraUsbPairLeftId || id == kCameraLegacyPairLeftId;
}

// ================================================================================

inline bool camera_id_is_usb_pair_right(const std::string &id) {
    return id == kCameraUsbPairRightId || id == kCameraLegacyPairRightId;
}

#endif
