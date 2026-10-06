#include "config.h"
#include "telemetry.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>
#include <utime.h>

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

static fs::path make_dir(const char *name) {
    const fs::path dir = fs::temp_directory_path()
            / (std::string("doggy-tel-") + name + "-" + std::to_string(getpid()));
    fs::remove_all(dir);
    return dir;
}

// ================================================================================

static DogStatus moving_sample(double latitude) {
    DogStatus sample;
    sample.set_type(doggy::v1::ROVER);
    sample.set_speed(1.0);
    sample.set_turn(0.2);
    sample.mutable_gps()->set_ok(true);
    sample.mutable_gps()->set_latitude_deg(latitude);
    sample.mutable_gps()->set_longitude_deg(34.8);
    return sample;
}

// ================================================================================

static int count_lines(const fs::path &path) {
    std::ifstream in(path);
    int lines = 0;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() == false) {
            lines += 1;
        }
    }
    return lines;
}

// ================================================================================

static int count_named(const fs::path &dir, std::size_t length, const std::string &prefix) {
    int count = 0;
    for (const fs::directory_entry &entry : fs::directory_iterator(dir)) {
        const std::string name = entry.path().filename().string();
        if (name.size() == length && name.compare(0, prefix.size(), prefix) == 0) {
            count += 1;
        }
    }
    return count;
}

// ================================================================================

static int plain_lines(const fs::path &dir) {
    int lines = 0;
    for (const fs::directory_entry &entry : fs::directory_iterator(dir)) {
        if (entry.path().filename().string().size() == 34) {
            lines += count_lines(entry.path());
        }
    }
    return lines;
}

// ================================================================================

int main() {
    const std::int64_t t0 = 1700000000;
    Config config;
    fill_config_defaults(config);

    {
        const fs::path dir = make_dir("rate");
        TelemetryLog log(dir.string(), t0);
        expect(log.ok(), "new telemetry log opens");
        expect(log.mission() == 1, "first mission is 1");
        expect(log.mission_start_unix() == t0, "mission start is the open time");
        DogStatus sample = moving_sample(32.0);
        log.sample_at(sample, config, t0);
        log.sample_at(sample, config, t0);
        sample.mutable_gps()->set_latitude_deg(32.1);
        log.sample_at(sample, config, t0 + 1);
        log.sample_at(sample, config, t0 + 2);
        log.flush();
        expect(count_named(dir, 34, "m0000000001-") == 1, "one open segment");
        expect(plain_lines(dir) == 2, "1 Hz cap skips duplicates and identical samples");
        const doggy::v1::TelemetryTrack track = log.track(2);
        expect(track.mission() == 1, "track reports the mission");
        expect(track.points_size() == 2, "track decimates to the requested cap");
        std::string zip_bytes;
        std::string zip_name;
        expect(log.download_zip(101, zip_bytes, zip_name) == false,
               "previous above 100 is rejected");
        expect(log.download_zip(0, zip_bytes, zip_name), "current mission downloads");
        expect(zip_bytes.size() > 4 && zip_bytes[0] == 'P' && zip_bytes[1] == 'K',
               "download is a zip");
        expect(zip_bytes.find('/') == std::string::npos, "zip entries are basenames");
        expect(zip_name.find("m0000000001") != std::string::npos,
               "download name includes the mission");
    }

    {
        const fs::path dir = make_dir("roll");
        TelemetryLog log(dir.string(), t0);
        log.sample_at(moving_sample(32.0), config, t0);
        log.sample_at(moving_sample(32.2), config, t0 + 3600);
        log.flush();
        expect(log.mission() == 1, "hourly rollover keeps the mission");
        expect(count_named(dir, 37, "m0000000001-") == 1, "rolled file is gzipped");
        expect(count_named(dir, 34, "m0000000001-") == 1, "new hour stays plain jsonl");
        expect(plain_lines(dir) == 1, "the sample after the hour is in the open file");
    }

    {
        const fs::path dir = make_dir("idle");
        TelemetryLog log(dir.string(), t0);
        DogStatus stopped;
        stopped.set_type(doggy::v1::ROVER);
        stopped.set_speed(0);
        stopped.set_turn(0);
        stopped.mutable_gps()->set_latitude_deg(32.0);
        stopped.mutable_gps()->set_longitude_deg(34.8);
        log.sample_at(stopped, config, t0);
        stopped.mutable_gps()->set_latitude_deg(32.01);
        log.sample_at(stopped, config, t0 + 600);
        log.flush();
        expect(log.mission() == 2, "ten minutes stopped starts a mission");
        expect(count_named(dir, 37, "m0000000001-") == 1, "old mission is gzipped");
        expect(count_named(dir, 34, "m0000000002-") == 1, "new mission file is plain");
    }

    {
        const fs::path dir = make_dir("dog");
        TelemetryLog log(dir.string(), t0);
        DogStatus dog;
        dog.set_type(doggy::v1::DOG);
        dog.mutable_gps()->set_latitude_deg(32.0);
        dog.mutable_gps()->set_longitude_deg(34.8);
        log.sample_at(dog, config, t0);
        dog.mutable_gps()->set_latitude_deg(32.01);
        log.sample_at(dog, config, t0 + 600);
        log.flush();
        expect(log.mission() == 1, "a dog does not split missions while idle");
        expect(count_named(dir, 37, "m") == 0, "dog idle does not gzip the open file");
    }

    {
        const fs::path dir = make_dir("life");
        {
            TelemetryLog first(dir.string(), t0);
            first.sample_at(moving_sample(32.0), config, t0);
            first.flush();
            expect(first.mission() == 1, "lifecycle counter starts at 1");
        }
        TelemetryLog second(dir.string(), t0 + 10);
        expect(second.mission() == 2, "mission counter survives a restart");
        std::string zip_bytes;
        std::string zip_name;
        expect(second.download_zip(1, zip_bytes, zip_name), "previous mission is included");
        expect(zip_bytes.find("m0000000001-") != std::string::npos,
               "zip contains the previous mission file");
        expect(zip_bytes.find("m0000000002-") != std::string::npos,
               "zip contains the current mission file");
    }

    {
        const fs::path dir = make_dir("bad");
        fs::create_directories(dir);
        {
            std::ofstream out(dir / "counter");
            out << "nope\n";
        }
        TelemetryLog log(dir.string(), t0);
        expect(log.ok() == false, "a corrupt counter disables telemetry");
    }

    {
        const fs::path dir = make_dir("keep");
        {
            TelemetryLog first(dir.string(), 1000);
            first.sample_at(moving_sample(32.0), config, 1000);
            first.flush();
        }
        fs::path old_gz;
        for (const fs::directory_entry &entry : fs::directory_iterator(dir)) {
            if (entry.path().filename().string().size() == 37) {
                old_gz = entry.path();
            }
        }
        expect(old_gz.empty() == false, "closed mission becomes gzip");
        struct utimbuf times{};
        times.actime = 0;
        times.modtime = 0;
        expect(utime(old_gz.c_str(), &times) == 0, "test can age a closed file");
        config.mutable_telemetry()->set_retain_hours(1);
        TelemetryLog second(dir.string(), 1000 + 7200);
        second.sample_at(moving_sample(32.2), config, 1000 + 7200);
        second.flush();
        expect(fs::exists(old_gz) == false, "retention deletes an old gzip");
        expect(count_named(dir, 34, "m0000000002-") == 1,
               "retention leaves the open file");
    }

    return failures == 0 ? 0 : 1;
}
