#include "doggy.h"

#include <filesystem>
#include <iostream>
#include <unistd.h>

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

int main() {
    Dog dog;
    dog.poll();
    const DogStatus first = dog.getStatus();
    dog.poll();
    const DogStatus second = dog.getStatus();

    expect(true, "poll does not throw");
    if (first.imu.ok) {
        expect(second.imu.ok, "open IMU stays ok across polls");
    } else {
        expect(second.imu.ok == false, "closed IMU stays not ok across polls");
    }

    if (first.battery.ok) {
        expect(second.battery.ok, "open ADC stays ok across polls");
    } else {
        expect(second.battery.ok == false, "closed ADC stays not ok across polls");
    }

    const std::filesystem::path dir =
            std::filesystem::temp_directory_path()
            / ("doggy-replace-config-" + std::to_string(getpid()));
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const std::string path = (dir / "doggy.json").string();

    Config next;
    next.servos.front_right_waist = 1;
    Dog stored(Config{}, path);
    expect(stored.replaceConfig(next) == CommandResult::ok,
           "replaceConfig writes when path is set");
    expect(stored.getConfig().servos.front_right_waist == 1,
           "replaceConfig stores the new channel");
    expect(stored.listServos().front().id == 1,
           "replaceConfig remaps the servo id now");
    const Config from_disk = Config::load_file(path);
    expect(from_disk.servos.front_right_waist == 1,
           "replaceConfig persists the channel to disk");

    Dog memory_only;
    Config skipped;
    skipped.servos.front_right_waist = 2;
    expect(memory_only.replaceConfig(skipped) == CommandResult::ok,
           "replaceConfig with empty path still remaps");
    expect(memory_only.getConfig().servos.front_right_waist == 2,
           "empty path still stores the new channel in memory");
    expect(memory_only.listServos().front().id == 2,
           "empty path still remaps the servo id");

    std::filesystem::remove_all(dir);

    if (failures != 0) {
        return 1;
    }

    return 0;
}
