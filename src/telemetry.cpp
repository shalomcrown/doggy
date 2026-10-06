#include "telemetry.h"

#include <plog/Log.h>

#include <nlohmann/json.hpp>

#include <zlib.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <queue>
#include <sstream>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {

TelemetryLog *g_telemetry = nullptr;

// ================================================================================

void append_u16(std::string &out, unsigned value) {
    out.push_back(static_cast<char>(value & 0xffu));
    out.push_back(static_cast<char>((value >> 8) & 0xffu));
}

// ================================================================================

void append_u32(std::string &out, unsigned value) {
    out.push_back(static_cast<char>(value & 0xffu));
    out.push_back(static_cast<char>((value >> 8) & 0xffu));
    out.push_back(static_cast<char>((value >> 16) & 0xffu));
    out.push_back(static_cast<char>((value >> 24) & 0xffu));
}

// ================================================================================

bool write_all(int fd, const char *data, std::size_t size) {
    std::size_t sent = 0;
    while (sent < size) {
        const ssize_t wrote = ::write(fd, data + sent, size - sent);
        if (wrote < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (wrote == 0) {
            return false;
        }
        sent += static_cast<std::size_t>(wrote);
    }
    return true;
}

// ================================================================================

bool fsync_path(const fs::path &path) {
    const int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        return false;
    }
    const int synced = ::fsync(fd);
    ::close(fd);
    return synced == 0;
}

// ================================================================================

std::string mission_file_name(std::uint64_t mission, std::int64_t unix_s) {
    const std::time_t when = static_cast<std::time_t>(unix_s);
    std::tm utc{};
    gmtime_r(&when, &utc);
    char stamp[32];
    if (std::strftime(stamp, sizeof(stamp), "%Y%m%dT%H%M%SZ", &utc) == 0) {
        return {};
    }
    char name[80];
    std::snprintf(
            name,
            sizeof(name),
            "m%010llu-%s.jsonl",
            static_cast<unsigned long long>(mission),
            stamp);
    return name;
}

// ================================================================================

bool parse_mission_name(const std::string &name, std::uint64_t &mission) {
    if (name.size() != 34 && name.size() != 37) {
        return false;
    }
    if (name[0] != 'm' || name[11] != '-') {
        return false;
    }
    for (int i = 1; i <= 10; ++i) {
        if (name[static_cast<std::size_t>(i)] < '0'
                || name[static_cast<std::size_t>(i)] > '9') {
            return false;
        }
    }
    if (name.compare(28, 6, ".jsonl") != 0) {
        return false;
    }
    if (name.size() == 37 && name.compare(34, 3, ".gz") != 0) {
        return false;
    }
    mission = std::strtoull(name.c_str() + 1, nullptr, 10);
    return mission > 0;
}

// ================================================================================

bool gzip_file(const fs::path &plain) {
    const fs::path gz = fs::path(plain.string() + ".gz");
    gzFile out = gzopen(gz.c_str(), "wb9");
    if (out == nullptr) {
        return false;
    }
    std::ifstream in(plain, std::ios::binary);
    if (in.is_open() == false) {
        gzclose(out);
        fs::remove(gz);
        return false;
    }
    char buffer[8192];
    bool failed = false;
    while (in.eof() == false) {
        in.read(buffer, sizeof(buffer));
        const std::streamsize got = in.gcount();
        if (got > 0 && gzwrite(out, buffer, static_cast<unsigned>(got)) == 0) {
            failed = true;
            break;
        }
        if (in.bad()) {
            failed = true;
            break;
        }
    }
    if (gzclose(out) != Z_OK) {
        failed = true;
    }
    if (failed || fsync_path(gz) == false) {
        fs::remove(gz);
        return false;
    }
    std::error_code error;
    fs::remove(plain, error);
    return error ? false : true;
}

// ================================================================================

std::string read_file_limited(const fs::path &path, std::size_t cap, bool &ok) {
    std::ifstream in(path, std::ios::binary);
    if (in.is_open() == false) {
        ok = false;
        return {};
    }
    std::ostringstream text;
    text << in.rdbuf();
    if (in.bad() || text.str().size() > cap) {
        ok = false;
        return {};
    }
    ok = true;
    return text.str();
}

// ================================================================================

std::string read_gzip_limited(const fs::path &path, std::size_t cap, bool &ok) {
    gzFile in = gzopen(path.c_str(), "rb");
    if (in == nullptr) {
        ok = false;
        return {};
    }
    std::string text;
    char buffer[4096];
    while (text.size() <= cap) {
        const int got = gzread(in, buffer, sizeof(buffer));
        if (got < 0) {
            gzclose(in);
            ok = false;
            return {};
        }
        if (got == 0) {
            break;
        }
        text.append(buffer, static_cast<std::size_t>(got));
    }
    gzclose(in);
    if (text.size() > cap) {
        ok = false;
        return {};
    }
    ok = true;
    return text;
}

// ================================================================================

void append_points(const std::string &text, std::vector<doggy::v1::TelemetryPoint> &points) {
    std::size_t cursor = 0;
    while (cursor < text.size()) {
        const std::size_t end = text.find('\n', cursor);
        const std::string line = text.substr(
                cursor,
                end == std::string::npos ? std::string::npos : end - cursor);
        cursor = end == std::string::npos ? text.size() : end + 1;
        if (line.empty()) {
            continue;
        }
        nlohmann::json parsed = nlohmann::json::parse(line, nullptr, false);
        if (parsed.is_object() == false || parsed.contains("gps") == false
                || parsed["gps"].is_object() == false) {
            continue;
        }
        const nlohmann::json &gps = parsed["gps"];
        if (gps.contains("latitude_deg") == false || gps.contains("longitude_deg") == false
                || gps["latitude_deg"].is_number() == false
                || gps["longitude_deg"].is_number() == false) {
            continue;
        }
        doggy::v1::TelemetryPoint point;
        point.set_latitude_deg(gps["latitude_deg"].get<double>());
        point.set_longitude_deg(gps["longitude_deg"].get<double>());
        if (parsed.contains("unix_time") && parsed["unix_time"].is_string()) {
            point.set_unix_time(std::strtoll(parsed["unix_time"].get<std::string>().c_str(), nullptr, 10));
        } else if (parsed.contains("unix_time") && parsed["unix_time"].is_number()) {
            point.set_unix_time(parsed["unix_time"].get<std::int64_t>());
        }
        points.push_back(std::move(point));
    }
}

// ================================================================================

void decimate(std::vector<doggy::v1::TelemetryPoint> &points, std::size_t max_points) {
    if (max_points == 0 || points.size() <= max_points) {
        return;
    }
    std::vector<doggy::v1::TelemetryPoint> kept;
    kept.reserve(max_points);
    const std::size_t step = (points.size() + max_points - 1) / max_points;
    for (std::size_t i = 0; i < points.size() && kept.size() < max_points; i += step) {
        kept.push_back(points[i]);
    }
    points.swap(kept);
}

// ================================================================================

std::string zip_store(const std::vector<std::pair<std::string, std::string>> &files) {
    std::string out;
    struct Entry {
        std::string name;
        std::uint32_t crc = 0;
        std::uint32_t size = 0;
        std::uint32_t offset = 0;
    };
    std::vector<Entry> entries;
    for (const auto &file : files) {
        Entry entry;
        entry.name = file.first;
        entry.crc = static_cast<std::uint32_t>(::crc32(
                0L,
                reinterpret_cast<const Bytef *>(file.second.data()),
                static_cast<uInt>(file.second.size())));
        entry.size = static_cast<std::uint32_t>(file.second.size());
        entry.offset = static_cast<std::uint32_t>(out.size());
        append_u32(out, 0x04034b50u);
        append_u16(out, 20);
        append_u16(out, 0);
        append_u16(out, 0);
        append_u16(out, 0);
        append_u16(out, 0);
        append_u32(out, entry.crc);
        append_u32(out, entry.size);
        append_u32(out, entry.size);
        append_u16(out, static_cast<unsigned>(entry.name.size()));
        append_u16(out, 0);
        out.append(entry.name);
        out.append(file.second);
        entries.push_back(std::move(entry));
    }
    const std::uint32_t central = static_cast<std::uint32_t>(out.size());
    for (const Entry &entry : entries) {
        append_u32(out, 0x02014b50u);
        append_u16(out, 20);
        append_u16(out, 20);
        append_u16(out, 0);
        append_u16(out, 0);
        append_u16(out, 0);
        append_u16(out, 0);
        append_u32(out, entry.crc);
        append_u32(out, entry.size);
        append_u32(out, entry.size);
        append_u16(out, static_cast<unsigned>(entry.name.size()));
        append_u16(out, 0);
        append_u16(out, 0);
        append_u16(out, 0);
        append_u16(out, 0);
        append_u32(out, 0);
        append_u32(out, entry.offset);
        out.append(entry.name);
    }
    const std::uint32_t central_size = static_cast<std::uint32_t>(out.size()) - central;
    append_u32(out, 0x06054b50u);
    append_u16(out, 0);
    append_u16(out, 0);
    append_u16(out, static_cast<unsigned>(entries.size()));
    append_u16(out, static_cast<unsigned>(entries.size()));
    append_u32(out, central_size);
    append_u32(out, central);
    append_u16(out, 0);
    return out;
}

// ================================================================================

std::uint64_t free_bytes(const fs::path &directory) {
    struct statvfs usage{};
    if (statvfs(directory.c_str(), &usage) != 0) {
        return 0;
    }
    return static_cast<std::uint64_t>(usage.f_bavail) * static_cast<std::uint64_t>(usage.f_frsize);
}

}  // namespace

// ================================================================================

struct TelemetryLog::Impl {
    std::string directory;
    mutable std::mutex mutex;
    bool ready = false;
    std::uint64_t mission = 0;
    std::int64_t mission_start = 0;
    std::int64_t file_open_unix = 0;
    std::int64_t last_unix = 0;
    std::int64_t last_flush_unix = 0;
    std::int64_t idle_s = 0;
    std::int64_t last_retention_unix = 0;
    std::string last_key;
    std::string pending;
    std::string open_name;
    int fd = -1;
    mutable std::condition_variable cv;
    mutable std::queue<fs::path> gzip_queue;
    mutable bool gzip_busy = false;
    mutable bool sync_now = false;
    std::thread worker;
    bool stop = false;

    // ================================================================================

    bool read_counter(std::uint64_t &value) const {
        const fs::path path = fs::path(directory) / "counter";
        std::ifstream in(path);
        if (in.is_open() == false) {
            value = 0;
            return true;
        }
        std::string text;
        std::getline(in, text);
        if (text.empty()) {
            value = 0;
            return true;
        }
        for (const char ch : text) {
            if (ch < '0' || ch > '9') {
                return false;
            }
        }
        value = std::strtoull(text.c_str(), nullptr, 10);
        return true;
    }

    // ================================================================================

    bool write_counter(std::uint64_t value) const {
        const fs::path dest = fs::path(directory) / "counter";
        const fs::path temp = fs::path(directory) / "counter.tmp";
        const int out = ::open(temp.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0644);
        if (out < 0) {
            return false;
        }
        const std::string text = std::to_string(value) + "\n";
        const bool wrote = write_all(out, text.data(), text.size());
        const bool synced = ::fsync(out) == 0;
        ::close(out);
        if (wrote == false || synced == false) {
            fs::remove(temp);
            return false;
        }
        std::error_code error;
        fs::rename(temp, dest, error);
        return error ? false : true;
    }

    // ================================================================================

    void write_pending() {
        if (fd < 0 || pending.empty()) {
            return;
        }
        if (write_all(fd, pending.data(), pending.size())) {
            pending.clear();
        }
    }

    // ================================================================================

    void queue_gzip(const fs::path &plain) {
        if (fs::is_regular_file(plain) == false) {
            return;
        }
        gzip_queue.push(plain);
        cv.notify_all();
    }

    // ================================================================================

    void close_segment(bool gzip) {
        write_pending();
        if (fd >= 0) {
            ::close(fd);
            fd = -1;
        }
        if (open_name.empty()) {
            return;
        }
        const fs::path plain = fs::path(directory) / open_name;
        open_name.clear();
        if (gzip) {
            queue_gzip(plain);
        }
    }

    // ================================================================================

    bool open_segment(std::int64_t unix_s) {
        const std::string name = mission_file_name(mission, unix_s);
        if (name.empty()) {
            return false;
        }
        const fs::path path = fs::path(directory) / name;
        const int opened = ::open(path.c_str(), O_CREAT | O_WRONLY | O_APPEND, 0644);
        if (opened < 0) {
            return false;
        }
        fd = opened;
        open_name = name;
        file_open_unix = unix_s;
        last_flush_unix = unix_s;
        return true;
    }

    // ================================================================================

    void gzip_leftovers() {
        std::error_code error;
        for (const fs::directory_entry &entry : fs::directory_iterator(directory, error)) {
            if (error || entry.is_regular_file() == false) {
                continue;
            }
            const std::string name = entry.path().filename().string();
            std::uint64_t ignored = 0;
            if (parse_mission_name(name, ignored) == false || name.size() != 34) {
                continue;
            }
            gzip_file(entry.path());
        }
    }

    // ================================================================================

    bool begin_mission(std::int64_t unix_s) {
        std::uint64_t current = 0;
        if (read_counter(current) == false) {
            PLOG_WARNING << "telemetry counter is unreadable";
            ready = false;
            return false;
        }
        if (current == UINT64_MAX) {
            ready = false;
            return false;
        }
        close_segment(true);
        if (write_counter(current + 1) == false) {
            ready = false;
            return false;
        }
        mission = current + 1;
        mission_start = unix_s;
        idle_s = 0;
        last_key.clear();
        if (open_segment(unix_s) == false) {
            ready = false;
            return false;
        }
        ready = true;
        return true;
    }

    // ================================================================================

    void enforce_retention(std::int64_t unix_s, const Config &config) {
        if (last_retention_unix != 0 && unix_s < last_retention_unix + 900) {
            return;
        }
        last_retention_unix = unix_s;
        const std::int64_t keep_s = static_cast<std::int64_t>(config.telemetry().retain_hours()) * 3600;
        struct ClosedFile {
            fs::path path;
            std::int64_t mtime = 0;
        };
        std::vector<ClosedFile> closed;
        std::error_code error;
        for (const fs::directory_entry &entry : fs::directory_iterator(directory, error)) {
            if (error || entry.is_regular_file() == false) {
                continue;
            }
            const std::string name = entry.path().filename().string();
            std::uint64_t ignored = 0;
            if (parse_mission_name(name, ignored) == false || name.size() != 37) {
                continue;
            }
            struct stat info{};
            if (::stat(entry.path().c_str(), &info) != 0) {
                continue;
            }
            ClosedFile file;
            file.path = entry.path();
            file.mtime = static_cast<std::int64_t>(info.st_mtime);
            if (unix_s >= file.mtime + keep_s) {
                fs::remove(file.path, error);
                continue;
            }
            closed.push_back(std::move(file));
        }
        const std::uint64_t floor = static_cast<std::uint64_t>(config.media().min_free_mb())
                * 1024ull * 1024ull;
        std::sort(closed.begin(), closed.end(), [](const ClosedFile &left, const ClosedFile &right) {
            return left.mtime < right.mtime;
        });
        for (const ClosedFile &file : closed) {
            if (free_bytes(directory) >= floor) {
                break;
            }
            fs::remove(file.path, error);
        }
    }

    // ================================================================================

    std::vector<fs::path> files_for_missions(const std::vector<std::uint64_t> &wanted) const {
        std::vector<fs::path> found;
        std::error_code error;
        for (const fs::directory_entry &entry : fs::directory_iterator(directory, error)) {
            if (error || entry.is_regular_file() == false) {
                continue;
            }
            const std::string name = entry.path().filename().string();
            std::uint64_t mission_id = 0;
            if (parse_mission_name(name, mission_id) == false) {
                continue;
            }
            bool keep = false;
            for (const std::uint64_t item : wanted) {
                if (item == mission_id) {
                    keep = true;
                }
            }
            if (keep) {
                found.push_back(entry.path());
            }
        }
        std::sort(found.begin(), found.end());
        return found;
    }

    // ================================================================================

    void worker_main() {
        for (;;) {
            fs::path next;
            int duped = -1;
            {
                std::unique_lock<std::mutex> lock(mutex);
                cv.wait_for(lock, std::chrono::seconds(1), [this] {
                    return stop || gzip_queue.empty() == false || sync_now;
                });
                if (gzip_queue.empty() == false) {
                    next = gzip_queue.front();
                    gzip_queue.pop();
                    gzip_busy = true;
                } else if (sync_now) {
                    write_pending();
                    if (fd >= 0) {
                        duped = ::dup(fd);
                    }
                    sync_now = false;
                    cv.notify_all();
                } else if (stop) {
                    cv.notify_all();
                    break;
                } else {
                    continue;
                }
            }
            if (next.empty() == false) {
                fsync_path(next);
                if (gzip_file(next) == false) {
                    PLOG_WARNING << "telemetry gzip failed for " << next.string();
                }
                std::lock_guard<std::mutex> lock(mutex);
                gzip_busy = false;
                cv.notify_all();
            }
            if (duped >= 0) {
                ::fsync(duped);
                ::close(duped);
            }
        }
    }
};

// ================================================================================

TelemetryLog::TelemetryLog(std::string directory)
        : TelemetryLog(std::move(directory), std::time(nullptr)) {}

// ================================================================================

TelemetryLog::TelemetryLog(std::string directory, std::int64_t start_unix) : impl_(new Impl) {
    impl_->directory = std::move(directory);
    std::error_code error;
    fs::create_directories(impl_->directory, error);
    if (error) {
        PLOG_WARNING << "telemetry directory: " << error.message();
        return;
    }
    impl_->gzip_leftovers();
    impl_->begin_mission(start_unix);
    if (impl_->ready) {
        impl_->worker = std::thread([this] { impl_->worker_main(); });
    }
}

// ================================================================================

TelemetryLog::~TelemetryLog() {
    close();
    delete impl_;
    impl_ = nullptr;
}

// ================================================================================

bool TelemetryLog::ok() const {
    if (impl_ == nullptr) {
        return false;
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->ready;
}

// ================================================================================

void TelemetryLog::await_writes() const {
    if (impl_ == nullptr) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->write_pending();
    }
    std::unique_lock<std::mutex> lock(impl_->mutex);
    impl_->cv.wait(lock, [this] {
        return impl_->gzip_queue.empty() && impl_->gzip_busy == false;
    });
}

// ================================================================================

void TelemetryLog::flush() const {
    if (impl_ == nullptr) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->write_pending();
        if (impl_->fd >= 0) {
            ::fsync(impl_->fd);
        }
    }
    await_writes();
}

// ================================================================================

void TelemetryLog::close() {
    if (impl_ == nullptr) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if (impl_->stop) {
            return;
        }
        impl_->close_segment(true);
        impl_->ready = false;
        impl_->stop = true;
        impl_->cv.notify_all();
    }
    if (impl_->worker.joinable()) {
        impl_->worker.join();
    }
}

// ================================================================================

void TelemetryLog::sample(const DogStatus &status, const Config &config) {
    sample_at(status, config, std::time(nullptr));
}

// ================================================================================

void TelemetryLog::sample_at(
        const DogStatus &status,
        const Config &config,
        std::int64_t unix_s) {
    if (impl_ == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->ready == false) {
        return;
    }
    if (impl_->last_unix != 0 && unix_s < impl_->last_unix + kTelemetryPeriodS) {
        return;
    }
    const std::int64_t gap = impl_->last_unix == 0 ? 0 : unix_s - impl_->last_unix;
    impl_->last_unix = unix_s;

    const bool stopped = status.type() == doggy::v1::ROVER
            && std::abs(status.speed()) < 1e-6
            && std::abs(status.turn()) < 1e-6;
    if (stopped) {
        impl_->idle_s += gap;
    } else {
        impl_->idle_s = 0;
    }

    if (impl_->idle_s >= kTelemetryIdleS) {
        impl_->begin_mission(unix_s);
    } else if (unix_s >= impl_->file_open_unix + kTelemetryRollS) {
        impl_->close_segment(true);
        impl_->open_segment(unix_s);
    }
    impl_->enforce_retention(unix_s, config);
    if (impl_->ready == false) {
        return;
    }

    DogStatus stored = status;
    stored.set_mission(impl_->mission);
    stored.set_mission_start_unix(impl_->mission_start);
    stored.set_unix_time(unix_s);
    DogStatus compare = stored;
    compare.clear_unix_time();
    std::string key;
    try {
        key = proto_to_json(compare);
    } catch (const std::exception &) {
        return;
    }
    if (key == impl_->last_key) {
        if (unix_s >= impl_->last_flush_unix + kTelemetryFlushS) {
            impl_->sync_now = true;
            impl_->last_flush_unix = unix_s;
            impl_->cv.notify_all();
        }
        return;
    }
    impl_->last_key = key;
    try {
        impl_->pending.append(proto_to_json(stored));
        impl_->pending.push_back('\n');
    } catch (const std::exception &) {
        return;
    }
    if (impl_->pending.size() >= 65536
            || unix_s >= impl_->last_flush_unix + kTelemetryFlushS) {
        impl_->write_pending();
        impl_->sync_now = true;
        impl_->last_flush_unix = unix_s;
        impl_->cv.notify_all();
    }
}

// ================================================================================

void TelemetryLog::start_mission() {
    if (impl_ == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->begin_mission(std::time(nullptr));
}

// ================================================================================

void TelemetryLog::attach(DogStatus &status) const {
    if (impl_ == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->ready == false) {
        return;
    }
    status.set_mission(impl_->mission);
    status.set_mission_start_unix(impl_->mission_start);
}

// ================================================================================

std::uint64_t TelemetryLog::mission() const {
    if (impl_ == nullptr) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->mission;
}

// ================================================================================

std::int64_t TelemetryLog::mission_start_unix() const {
    if (impl_ == nullptr) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->mission_start;
}

// ================================================================================

doggy::v1::TelemetryTrack TelemetryLog::track(std::size_t max_points) const {
    doggy::v1::TelemetryTrack result;
    if (impl_ == nullptr) {
        return result;
    }
    await_writes();
    std::lock_guard<std::mutex> lock(impl_->mutex);
    result.set_mission(impl_->mission);
    result.set_mission_start_unix(impl_->mission_start);
    std::vector<doggy::v1::TelemetryPoint> points;
    const std::vector<fs::path> files = impl_->files_for_missions({impl_->mission});
    for (const fs::path &path : files) {
        bool read_ok = false;
        const bool gz = path.extension() == ".gz";
        const std::string text = gz
                ? read_gzip_limited(path, 8u * 1024u * 1024u, read_ok)
                : read_file_limited(path, 8u * 1024u * 1024u, read_ok);
        if (read_ok) {
            append_points(text, points);
        }
    }
    if (max_points > 5000) {
        max_points = 5000;
    }
    decimate(points, max_points);
    for (const doggy::v1::TelemetryPoint &point : points) {
        *result.add_points() = point;
    }
    return result;
}

// ================================================================================

bool TelemetryLog::download_zip(
        int previous,
        std::string &bytes,
        std::string &download_name) const {
    bytes.clear();
    download_name.clear();
    if (impl_ == nullptr || previous < 0 || previous > kTelemetryMaxPrevious) {
        return false;
    }
    await_writes();
    std::lock_guard<std::mutex> lock(impl_->mutex);
    std::vector<std::uint64_t> wanted;
    wanted.push_back(impl_->mission);
    for (int i = 1; i <= previous; ++i) {
        if (impl_->mission > static_cast<std::uint64_t>(i)) {
            wanted.push_back(impl_->mission - static_cast<std::uint64_t>(i));
        }
    }
    std::vector<std::pair<std::string, std::string>> files;
    std::size_t total = 0;
    for (const fs::path &path : impl_->files_for_missions(wanted)) {
        bool read_ok = false;
        std::string data = read_file_limited(path, kTelemetryZipMaxBytes, read_ok);
        if (read_ok == false) {
            return false;
        }
        total += data.size();
        if (total > kTelemetryZipMaxBytes) {
            return false;
        }
        files.emplace_back(path.filename().string(), std::move(data));
    }
    if (files.empty()) {
        return false;
    }
    bytes = zip_store(files);
    char name[64];
    std::snprintf(
            name,
            sizeof(name),
            "doggy-m%010llu.zip",
            static_cast<unsigned long long>(impl_->mission));
    download_name = name;
    return true;
}

// ================================================================================

void telemetry_bind(TelemetryLog *log) {
    g_telemetry = log;
}

// ================================================================================

TelemetryLog *telemetry_current() {
    return g_telemetry;
}

// ================================================================================

void telemetry_attach_status(DogStatus &status) {
    if (g_telemetry == nullptr) {
        return;
    }
    g_telemetry->attach(status);
}

// ================================================================================

std::string telemetry_default_directory() {
    const char *env = std::getenv("DOGGY_TELEMETRY_DIR");
    if (env != nullptr && env[0] != '\0') {
        return env;
    }
    return "/var/lib/doggy/telemetry";
}
