#include "doggy_log.h"

#include <plog/Appenders/ColorConsoleAppender.h>
#include <plog/Appenders/IAppender.h>
#include <plog/Formatters/TxtFormatter.h>
#include <plog/Init.h>
#include <plog/Log.h>

#include <zlib.h>

#include <algorithm>
#include <ctime>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

// ================================================================================

namespace {

bool gzip_file(const fs::path &src, const fs::path &dst) {
    std::ifstream in(src, std::ios::binary);
    if (in.is_open() == false) {
        return false;
    }

    const fs::path tmp = dst.string() + ".tmp";
    gzFile out = gzopen(tmp.string().c_str(), "wb");
    if (out == nullptr) {
        return false;
    }

    char buf[8192];
    bool ok = true;
    while (in.good()) {
        in.read(buf, sizeof(buf));
        const std::streamsize n = in.gcount();
        if (n <= 0) {
            break;
        }

        if (gzwrite(out, buf, static_cast<unsigned int>(n)) != static_cast<int>(n)) {
            ok = false;
            break;
        }
    }

    if (gzclose(out) != Z_OK) {
        ok = false;
    }

    if (ok == false) {
        std::error_code ec;
        fs::remove(tmp, ec);
        return false;
    }

    std::error_code rename_ec;
    fs::rename(tmp, dst, rename_ec);
    if (rename_ec) {
        std::error_code ec;
        fs::remove(tmp, ec);
        return false;
    }

    return true;
}

std::string format_roll_stamp() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    if (localtime_r(&now, &local) == nullptr) {
        return "unknown";
    }

    char buf[16];
    if (std::strftime(buf, sizeof(buf), "%Y-%m-%d-%H%M", &local) == 0) {
        return "unknown";
    }

    return buf;
}

bool is_rolled_archive(const std::string &name) {
    const std::string dated_prefix = "doggy-";
    const std::string dated_suffix = ".log.gz";
    if (name.size() > dated_prefix.size() + dated_suffix.size()
            && name.compare(0, dated_prefix.size(), dated_prefix) == 0
            && name.compare(
                    name.size() - dated_suffix.size(),
                    dated_suffix.size(),
                    dated_suffix) == 0) {
        return true;
    }

    const std::string legacy_prefix = "doggy.log.";
    const std::string gzip_suffix = ".gz";
    if (name.size() > legacy_prefix.size() + gzip_suffix.size()
            && name.compare(0, legacy_prefix.size(), legacy_prefix) == 0
            && name.compare(
                    name.size() - gzip_suffix.size(),
                    gzip_suffix.size(),
                    gzip_suffix) == 0) {
        return true;
    }

    return false;
}

fs::path unique_archive_path(const fs::path &dir) {
    const std::string stamp = format_roll_stamp();
    fs::path path = dir / ("doggy-" + stamp + ".log.gz");
    int n = 2;
    std::error_code exists_ec;
    while (fs::exists(path, exists_ec)) {
        path = dir / ("doggy-" + stamp + "-" + std::to_string(n) + ".log.gz");
        n += 1;
    }

    return path;
}

void prune_archives(const fs::path &dir, int max_archives) {
    std::vector<fs::path> files;
    std::error_code it_ec;
    for (const fs::directory_entry &entry : fs::directory_iterator(dir, it_ec)) {
        std::error_code file_ec;
        if (entry.is_regular_file(file_ec) == false || file_ec.value() != 0) {
            continue;
        }

        if (is_rolled_archive(entry.path().filename().string()) == false) {
            continue;
        }

        files.push_back(entry.path());
    }

    std::sort(files.begin(), files.end(), [](const fs::path &a, const fs::path &b) {
        std::error_code a_ec;
        std::error_code b_ec;
        const auto a_time = fs::last_write_time(a, a_ec);
        const auto b_time = fs::last_write_time(b, b_ec);
        if (a_ec.value() == 0 && b_ec.value() == 0 && a_time != b_time) {
            return a_time < b_time;
        }

        return a.filename().string() < b.filename().string();
    });

    std::error_code rm_ec;
    while (static_cast<int>(files.size()) > max_archives) {
        fs::remove(files.front(), rm_ec);
        files.erase(files.begin());
    }
}

class GzipRollingAppender : public plog::IAppender {
public:
    GzipRollingAppender(std::string directory, std::size_t max_bytes, int max_files) :
        directory(std::move(directory)),
        max_bytes(max_bytes < 1000 ? 1000 : max_bytes),
        max_files(max_files < 2 ? 2 : max_files) {
        open_current();
    }

    bool is_open() const {
        return stream.is_open();
    }

    void write(const plog::Record &record) override {
        std::lock_guard<std::mutex> lock(mutex);
        if (stream.is_open() == false) {
            return;
        }

        stream << plog::TxtFormatter::format(record);
        stream.flush();
        const std::streampos pos = stream.tellp();
        if (pos > 0 && static_cast<std::size_t>(pos) > max_bytes) {
            roll_locked();
        }
    }

private:
    void open_current() {
        const fs::path path = doggy_log_path(directory);
        stream.open(path, std::ios::out | std::ios::app);
        if (stream.is_open() == false) {
            return;
        }

        std::error_code ec;
        fs::permissions(
                path,
                fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read,
                fs::perm_options::replace,
                ec);
    }

    void roll_locked() {
        if (stream.is_open()) {
            stream.close();
        }

        roll_doggy_log_file(directory, max_files);
        open_current();
    }

    std::string directory;
    std::size_t max_bytes;
    int max_files;
    std::mutex mutex;
    std::ofstream stream;
};

GzipRollingAppender *g_file_appender = nullptr;
plog::ColorConsoleAppender<plog::TxtFormatter> g_console_appender;
bool g_started = false;

}  // namespace

// ================================================================================

std::string default_doggy_log_dir() {
    if (const char *env = std::getenv("DOGGY_LOG_DIR")) {
        if (env[0] != '\0') {
            return env;
        }
    }

    return "/var/log/doggy";
}

// ================================================================================

std::string doggy_log_path(const std::string &directory) {
    return (fs::path(directory) / kDoggyLogFileName).string();
}

// ================================================================================

bool roll_doggy_log_file(const std::string &directory, int max_files) {
    if (max_files < 2) {
        max_files = 2;
    }

    const fs::path dir(directory);
    const fs::path current = dir / kDoggyLogFileName;
    std::error_code exists_ec;
    if (fs::exists(current, exists_ec) == false || exists_ec) {
        return true;
    }

    std::error_code size_ec;
    const std::uintmax_t size = fs::file_size(current, size_ec);
    if (size_ec || size == 0) {
        return true;
    }

    const int archives = max_files - 1;
    const fs::path gz = unique_archive_path(dir);
    if (gzip_file(current, gz) == false) {
        return false;
    }

    std::error_code rm_ec;
    fs::remove(current, rm_ec);
    prune_archives(dir, archives);
    return true;
}

// ================================================================================

bool init_doggy_log() {
    DoggyLogOptions options;
    options.directory = default_doggy_log_dir();
    return init_doggy_log(options);
}

// ================================================================================

bool init_doggy_log(const DoggyLogOptions &options) {
    if (g_started) {
        return g_file_appender != nullptr && g_file_appender->is_open();
    }

    std::string directory = options.directory;
    if (directory.empty()) {
        directory = default_doggy_log_dir();
    }

    std::error_code dir_ec;
    fs::create_directories(directory, dir_ec);

    if (options.roll_on_start) {
        roll_doggy_log_file(directory, options.max_files);
    }

    static GzipRollingAppender file_appender(directory, options.max_bytes, options.max_files);
    g_file_appender = &file_appender;

    if (file_appender.is_open()) {
        plog::init(plog::info, &file_appender);
        g_started = true;
        return true;
    }

    plog::init(plog::info, &g_console_appender);
    g_started = true;
    return false;
}
