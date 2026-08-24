#include "doggy_log.h"

#include <plog/Log.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <regex>
#include <string>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

// ================================================================================

static int failures = 0;

static void expect(bool cond, const char *name) {
    if (cond) {
        std::cout << "PASS " << name << std::endl;
        return;
    }

    std::cout << "FAIL " << name << std::endl;
    failures += 1;
}

// ================================================================================

static bool is_gzip(const fs::path &path) {
    std::ifstream in(path, std::ios::binary);
    unsigned char magic[2] = {0, 0};
    in.read(reinterpret_cast<char *>(magic), 2);
    return in.gcount() == 2 && magic[0] == 0x1f && magic[1] == 0x8b;
}

// ================================================================================

static std::string read_all(const fs::path &path) {
    std::ifstream in(path, std::ios::binary);
    if (in.is_open() == false) {
        return {};
    }

    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// ================================================================================

static fs::path make_temp_dir(const char *prefix) {
    const fs::path dir =
            fs::temp_directory_path() / (std::string(prefix) + std::to_string(getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

// ================================================================================

static std::vector<fs::path> dated_archives(const fs::path &dir) {
    static const std::regex re(
            R"(doggy-[0-9]{4}-[0-9]{2}-[0-9]{2}-[0-9]{4}(-[0-9]+)?\.log\.gz)");
    std::vector<fs::path> out;
    for (const fs::directory_entry &entry : fs::directory_iterator(dir)) {
        if (entry.is_regular_file() == false) {
            continue;
        }

        const std::string name = entry.path().filename().string();
        if (std::regex_match(name, re)) {
            out.push_back(entry.path());
        }
    }

    return out;
}

// ================================================================================

int main() {
    const fs::path roll_dir = make_temp_dir("doggy-log-roll-");
    const fs::path current = roll_dir / kDoggyLogFileName;
    {
        std::ofstream out(current);
        out << "first-line\n";
    }

    expect(roll_doggy_log_file(roll_dir.string(), kDoggyLogMaxFiles),
           "roll of a non-empty log succeeds");
    expect(fs::exists(current) == false, "current log is removed after roll");

    const std::vector<fs::path> first = dated_archives(roll_dir);
    expect(first.size() == 1, "roll produces one dated gzip archive");
    expect(first.empty() == false && is_gzip(first[0]),
           "rolled archive is gzip-compressed");
    expect(first.empty() == false
                   && first[0].filename().string().find("doggy-") == 0
                   && first[0].filename().string().find(".log.gz") != std::string::npos,
           "archive name is doggy-YYYY-MM-DD-HHMM.log.gz");

    int archive = 1;
    while (archive <= 9) {
        const std::string name =
                "doggy-2020-01-01-000" + std::to_string(archive) + ".log.gz";
        std::ofstream dummy(roll_dir / name);
        dummy << "x";
        archive += 1;
    }

    {
        std::ofstream out(current);
        out << "keep-ten\n";
    }

    expect(roll_doggy_log_file(roll_dir.string(), kDoggyLogMaxFiles),
           "roll with nine archives succeeds");
    expect(fs::exists(roll_dir / "doggy-2020-01-01-0001.log.gz") == false,
           "oldest archive is dropped");
    expect(dated_archives(roll_dir).size() <= static_cast<std::size_t>(kDoggyLogMaxFiles - 1),
           "keeps at most 9 gzip archives");

    int file_count = 0;
    for (const fs::directory_entry &entry : fs::directory_iterator(roll_dir)) {
        if (entry.is_regular_file()) {
            file_count += 1;
        }
    }

    expect(file_count <= kDoggyLogMaxFiles, "at most 10 log files after roll");

    const fs::path start_dir = make_temp_dir("doggy-log-start-");
    {
        std::ofstream out(start_dir / kDoggyLogFileName);
        out << "from-previous-run\n";
    }

    DoggyLogOptions options;
    options.directory = start_dir.string();
    options.max_bytes = 1000;
    options.max_files = kDoggyLogMaxFiles;
    options.roll_on_start = true;
    expect(init_doggy_log(options), "init_doggy_log opens the log file");
    expect(dated_archives(start_dir).empty() == false, "start rolls the previous log");
    expect(dated_archives(start_dir).empty() == false && is_gzip(dated_archives(start_dir)[0]),
           "start archive is gzip");
    expect(read_all(start_dir / kDoggyLogFileName).find("from-previous-run") == std::string::npos,
           "current log does not keep the previous run");

    int i = 0;
    while (i < 80) {
        PLOG_INFO << "padding-line-" << i << " xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";
        i += 1;
    }

    expect(dated_archives(start_dir).empty() == false, "size roll still has gzip archives");

    int start_count = 0;
    for (const fs::directory_entry &entry : fs::directory_iterator(start_dir)) {
        if (entry.is_regular_file()) {
            start_count += 1;
        }
    }

    expect(start_count <= kDoggyLogMaxFiles, "size roll keeps at most 10 files");

    fs::remove_all(roll_dir);
    fs::remove_all(start_dir);

    if (failures != 0) {
        return 1;
    }

    return 0;
}
