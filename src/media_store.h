#ifndef MEDIA_STORE_H
#define MEDIA_STORE_H

#include "doggy.pb.h"
#include "robot_api.h"

#include <ctime>
#include <optional>
#include <string>

inline constexpr const char *kDefaultRecordingsDir = "/var/lib/doggy/recordings";
inline constexpr const char *kDefaultSnapshotsDir = "/var/lib/doggy/snapshots";
inline constexpr const char *kDefaultFfmpegPath = "/usr/bin/ffmpeg";
inline constexpr int kSnapshotTimeoutMs = 8000;

// ================================================================================

bool media_name_allowed(const std::string &name);

// ================================================================================

std::string sanitize_media_hostname(const std::string &host);

// ================================================================================

std::string media_utc_stamp(std::time_t when);

// ================================================================================

class MediaStore {
public:
    MediaStore();
    MediaStore(
            std::string recordings_dir,
            std::string snapshots_dir,
            std::string ffmpeg_path);

    doggy::v1::MediaFileList listRecordings() const;
    doggy::v1::MediaFileList listSnapshots() const;
    std::optional<std::string> snapshotFile(const std::string &name) const;
    std::optional<std::string> copyRecording(const std::string &name) const;
    CommandResult takeSnapshot(
            const std::string &rtsp_url,
            const std::string &hostname,
            const std::string &camera_id);

private:
    std::string recordings_dir_;
    std::string snapshots_dir_;
    std::string ffmpeg_path_;
};

#endif
