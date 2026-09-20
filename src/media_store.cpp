#include "media_store.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <csignal>
#include <ctime>
#include <fcntl.h>
#include <filesystem>
#include <poll.h>
#include <regex>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

// ================================================================================

static const std::regex kMediaName{
        R"(^[A-Za-z0-9][A-Za-z0-9.-]{0,180}-[0-9]{4}-[0-9]{2}-[0-9]{2}-[0-9]{6}\.(ts|jpg)$)"
};

// ================================================================================

bool media_name_allowed(const std::string &name) {
    if (name.empty() || name.size() > 240) {
        return false;
    }
    if (name.find('/') != std::string::npos
            || name.find('\\') != std::string::npos
            || name.find('\0') != std::string::npos
            || name.rfind("..", 0) == 0
            || name.find("..") != std::string::npos) {
        return false;
    }
    return std::regex_match(name, kMediaName);
}

// ================================================================================

std::string sanitize_media_hostname(const std::string &host) {
    std::string out;
    out.reserve(host.size());
    for (char ch : host) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (std::isalnum(c) != 0 || ch == '.' || ch == '-') {
            out.push_back(ch);
        } else {
            out.push_back('-');
        }
    }
    while (out.empty() == false && (out.front() == '-' || out.front() == '.')) {
        out.erase(out.begin());
    }
    while (out.empty() == false && (out.back() == '-' || out.back() == '.')) {
        out.pop_back();
    }
    if (out.empty()) {
        return "doggy";
    }
    if (std::isalnum(static_cast<unsigned char>(out.front())) == 0) {
        return "h" + out;
    }
    if (out.size() > 64) {
        out.resize(64);
    }
    return out;
}

// ================================================================================

std::string media_utc_stamp(std::time_t when) {
    std::tm utc{};
    if (gmtime_r(&when, &utc) == nullptr) {
        return "1970-01-01-000000";
    }
    char buffer[32];
    if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%d-%H%M%S", &utc) == 0) {
        return "1970-01-01-000000";
    }
    return buffer;
}

// ================================================================================

static std::string env_or(const char *name, const char *fallback) {
    const char *value = std::getenv(name);
    if (value == nullptr || value[0] == '\0') {
        return fallback;
    }
    return value;
}

// ================================================================================

static bool is_regular_nofollow(const fs::path &path) {
    const int fd = open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }
    struct stat info {};
    const int ok = fstat(fd, &info);
    close(fd);
    return ok == 0 && S_ISREG(info.st_mode);
}

// ================================================================================

static std::optional<fs::path> allowed_file(
        const fs::path &dir,
        const std::string &name) {
    if (media_name_allowed(name) == false) {
        return std::nullopt;
    }
    const fs::path path = dir / name;
    if (is_regular_nofollow(path) == false) {
        return std::nullopt;
    }
    return path;
}

// ================================================================================

static doggy::v1::MediaFileList list_dir(
        const fs::path &dir,
        const char *extension) {
    doggy::v1::MediaFileList list;
    std::error_code error;
    std::vector<fs::directory_entry> entries;
    for (const fs::directory_entry &entry : fs::directory_iterator(dir, error)) {
        entries.push_back(entry);
    }
    std::sort(
            entries.begin(),
            entries.end(),
            [](const fs::directory_entry &left, const fs::directory_entry &right) {
                return left.path().filename().string()
                        > right.path().filename().string();
            });
    for (const fs::directory_entry &entry : entries) {
        const std::string name = entry.path().filename().string();
        if (name.empty() || name.front() == '.') {
            continue;
        }
        const std::string ext(extension);
        if (name.size() < ext.size()
                || name.compare(name.size() - ext.size(), ext.size(), ext) != 0) {
            continue;
        }
        const std::optional<fs::path> path = allowed_file(dir, name);
        if (path.has_value() == false) {
            continue;
        }
        std::error_code stat_error;
        const auto size = fs::file_size(*path, stat_error);
        if (stat_error) {
            continue;
        }
        const auto mtime = fs::last_write_time(*path, stat_error);
        if (stat_error) {
            continue;
        }
        const auto system_time = std::chrono::clock_cast<std::chrono::system_clock>(
                mtime);
        doggy::v1::MediaFile *item = list.add_items();
        item->set_name(name);
        item->set_size(size);
        item->set_mtime_unix(std::chrono::system_clock::to_time_t(system_time));
    }
    return list;
}

// ================================================================================

static void close_extra_descriptors() {
#if defined(__linux__) && defined(SYS_close_range)
    if (syscall(SYS_close_range, STDERR_FILENO + 1, ~0U, 0) == 0) {
        return;
    }
#endif
    const long limit = sysconf(_SC_OPEN_MAX);
    const int last = limit > 0 && limit < 4096 ? static_cast<int>(limit) : 4096;
    for (int fd = STDERR_FILENO + 1; fd < last; ++fd) {
        close(fd);
    }
}

// ================================================================================

MediaStore::MediaStore() :
    MediaStore(
            env_or("DOGGY_RECORDINGS_DIR", kDefaultRecordingsDir),
            env_or("DOGGY_SNAPSHOTS_DIR", kDefaultSnapshotsDir),
            env_or("DOGGY_FFMPEG", kDefaultFfmpegPath)) {
}

// ================================================================================

MediaStore::MediaStore(
        std::string recordings_dir,
        std::string snapshots_dir,
        std::string ffmpeg_path) :
    recordings_dir_(std::move(recordings_dir)),
    snapshots_dir_(std::move(snapshots_dir)),
    ffmpeg_path_(std::move(ffmpeg_path)) {
}

// ================================================================================

doggy::v1::MediaFileList MediaStore::listRecordings() const {
    return list_dir(recordings_dir_, ".ts");
}

// ================================================================================

doggy::v1::MediaFileList MediaStore::listSnapshots() const {
    return list_dir(snapshots_dir_, ".jpg");
}

// ================================================================================

std::optional<std::string> MediaStore::snapshotFile(const std::string &name) const {
    const std::optional<fs::path> path = allowed_file(snapshots_dir_, name);
    if (path.has_value() == false) {
        return std::nullopt;
    }
    if (name.size() < 4 || name.compare(name.size() - 4, 4, ".jpg") != 0) {
        return std::nullopt;
    }
    return path->string();
}

// ================================================================================

std::optional<std::string> MediaStore::copyRecording(const std::string &name) const {
    const std::optional<fs::path> path = allowed_file(recordings_dir_, name);
    if (path.has_value() == false) {
        return std::nullopt;
    }
    if (name.size() < 3 || name.compare(name.size() - 3, 3, ".ts") != 0) {
        return std::nullopt;
    }
    const fs::path dest =
            fs::path(recordings_dir_)
            / (".dl-" + std::to_string(getpid()) + "-" + name);
    std::error_code error;
    fs::copy_file(*path, dest, fs::copy_options::overwrite_existing, error);
    if (error) {
        return std::nullopt;
    }
    return dest.string();
}

// ================================================================================

CommandResult MediaStore::takeSnapshot(
        const std::string &rtsp_url,
        const std::string &hostname,
        const std::string &camera_id) {
    if (rtsp_url.empty() || access(ffmpeg_path_.c_str(), X_OK) != 0) {
        return CommandResult::failed;
    }
    std::error_code error;
    fs::create_directories(snapshots_dir_, error);
    if (error && fs::is_directory(snapshots_dir_) == false) {
        return CommandResult::failed;
    }

    // Same grammar as MediaMTX recordings: host, camera id, then a UTC stamp.
    const std::string prefix = sanitize_media_hostname(hostname) + "-"
            + sanitize_media_hostname(camera_id);
    std::string dest;
    for (int offset = 0; offset < 10; ++offset) {
        const std::string name =
                prefix + "-" + media_utc_stamp(std::time(nullptr) + offset) + ".jpg";
        const fs::path candidate = fs::path(snapshots_dir_) / name;
        if (fs::exists(candidate) == false) {
            dest = candidate.string();
            break;
        }
    }
    if (dest.empty()) {
        return CommandResult::failed;
    }

    std::vector<std::string> command = {
        ffmpeg_path_,
        "-nostdin",
        "-y",
        "-hide_banner",
        "-loglevel", "error",
        "-rtsp_transport", "tcp",
        "-i", rtsp_url,
        "-an",
        "-frames:v", "1",
        "-q:v", "3",
        dest
    };
    std::vector<char *> argv;
    argv.reserve(command.size() + 1);
    for (std::string &item : command) {
        argv.push_back(item.data());
    }
    argv.push_back(nullptr);

    const pid_t pid = fork();
    if (pid == 0) {
        close_extra_descriptors();
        execv(argv[0], argv.data());
        _exit(127);
    }
    if (pid < 0) {
        return CommandResult::failed;
    }

    const auto deadline = std::chrono::steady_clock::now()
            + std::chrono::milliseconds(kSnapshotTimeoutMs);
    int status = 0;
    while (true) {
        const pid_t waited = waitpid(pid, &status, WNOHANG);
        if (waited == pid) {
            break;
        }
        if (waited < 0 && errno != EINTR) {
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            return CommandResult::failed;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            return CommandResult::failed;
        }
        poll(nullptr, 0, 50);
    }

    if (WIFEXITED(status) == false || WEXITSTATUS(status) != 0
            || is_regular_nofollow(dest) == false) {
        std::error_code remove_error;
        fs::remove(dest, remove_error);
        return CommandResult::failed;
    }
    return CommandResult::ok;
}
