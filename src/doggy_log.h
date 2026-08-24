#ifndef DOGGY_LOG_H
#define DOGGY_LOG_H

#include <cstddef>
#include <string>

// ================================================================================

inline constexpr std::size_t kDoggyLogMaxBytes = 1024 * 1024 * 10;
inline constexpr int kDoggyLogMaxFiles = 10;
inline constexpr const char kDoggyLogFileName[] = "doggy.log";

// ================================================================================

class DoggyLogOptions {
public:
    std::string directory;
    std::size_t max_bytes = kDoggyLogMaxBytes;
    int max_files = kDoggyLogMaxFiles;
    bool roll_on_start = true;
};

// ================================================================================

std::string default_doggy_log_dir();

std::string doggy_log_path(const std::string &directory);

// Rotate doggy.log into doggy-YYYY-MM-DD-HHMM.log.gz (local time of the roll).
// Keeps at most max_files-1 gzip archives. No-op if the current log is missing
// or empty.
bool roll_doggy_log_file(const std::string &directory, int max_files);

bool init_doggy_log();
bool init_doggy_log(const DoggyLogOptions &options);

#endif
