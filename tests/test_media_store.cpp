#include "media_store.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unistd.h>

namespace fs = std::filesystem;

static int failures = 0;

// ================================================================================

static void expect(bool ok, const char *what) {
    if (ok) {
        return;
    }
    std::cerr << "FAIL " << what << std::endl;
    failures += 1;
}

// ================================================================================

int main() {
    expect(media_name_allowed("pi-2026-09-20-093301.ts"),
           "hostname-timestamp MPEG-TS names are allowed");
    expect(media_name_allowed("doggy-1-cam0-2026-09-20-093301.ts"),
           "hostname-camera-timestamp MPEG-TS names are allowed");
    expect(media_name_allowed("pi-2026-09-20-093301.jpg"),
           "hostname-timestamp JPEG names are allowed");
    expect(media_name_allowed("../etc/passwd") == false,
           "path traversal names are rejected");
    expect(media_name_allowed("pi-2026-09-20-093301") == false,
           "extensionless names are rejected");
    expect(media_name_allowed("pi-2026-09-20-0933.ts") == false,
           "timestamps without seconds are rejected");
    expect(sanitize_media_hostname("pi 5!") == "pi-5",
           "hostnames drop unsafe characters");
    expect(sanitize_media_hostname("") == "doggy",
           "empty hostname becomes doggy");
    expect(media_utc_stamp(0) == "1970-01-01-000000",
           "UTC stamp includes seconds");

    const fs::path root = fs::temp_directory_path()
            / ("doggy-media-" + std::to_string(getpid()));
    const fs::path recordings = root / "recordings";
    const fs::path snapshots = root / "snapshots";
    fs::create_directories(recordings);
    fs::create_directories(snapshots);

    const std::string ts_name = "pi-2026-09-20-093301.ts";
    {
        std::ofstream out(recordings / ts_name, std::ios::binary);
        out << "mpegts";
    }
    {
        std::ofstream out(snapshots / "pi-2026-09-20-093302.jpg", std::ios::binary);
        out << "jpeg";
    }
    {
        std::ofstream out(recordings / "not-a-recording.bin", std::ios::binary);
        out << "nope";
    }

    MediaStore store(recordings.string(), snapshots.string(), "/bin/false");
    const auto listed = store.listRecordings();
    expect(listed.items_size() == 1 && listed.items(0).name() == ts_name,
           "recordings list returns only allowed .ts files");
    expect(store.listSnapshots().items_size() == 1,
           "snapshots list returns allowed JPEGs");
    expect(store.snapshotFile("../etc/passwd").has_value() == false,
           "snapshot download rejects traversal");
    expect(store.snapshotFile("pi-2026-09-20-093301.ts").has_value() == false,
           "snapshot download rejects MPEG-TS names");
    const std::optional<std::string> copy = store.copyRecording(ts_name);
    expect(copy.has_value() && fs::exists(*copy),
           "recording download copies the live file first");
    expect(store.copyRecording("nope.ts").has_value() == false,
           "missing recordings are not copied");

    const fs::path ffmpeg = root / "ffmpeg";
    {
        std::ofstream out(ffmpeg);
        out << "#!/bin/sh\n"
            << "for a in \"$@\"; do\n"
            << "  case \"$a\" in *.jpg) printf JPEG > \"$a\" ;; esac\n"
            << "done\n";
    }
    fs::permissions(ffmpeg, fs::perms::owner_exec | fs::perms::owner_read);
    MediaStore capturing(recordings.string(), snapshots.string(), ffmpeg.string());
    expect(capturing.takeSnapshot("rtsp://127.0.0.1:8554/cam0", "pi", "cam0")
                    == CommandResult::ok,
           "snapshot writes a JPEG via ffmpeg");
    const auto after_capture = capturing.listSnapshots();
    expect(after_capture.items_size() == 2,
           "new snapshot appears in the list");
    bool named_for_camera = false;
    for (const doggy::v1::MediaFile &item : after_capture.items()) {
        if (item.name().rfind("pi-cam0-", 0) == 0) {
            named_for_camera = true;
        }
    }
    expect(named_for_camera,
           "snapshot name carries the hostname and camera id");

    fs::remove_all(root);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
