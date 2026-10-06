#ifndef TELEMETRY_H
#define TELEMETRY_H

#include "config.h"

#include <cstdint>
#include <string>

inline constexpr int kTelemetryPeriodS = 1;
inline constexpr int kTelemetryFlushS = 10;
inline constexpr int kTelemetryRollS = 3600;
inline constexpr int kTelemetryIdleS = 600;
inline constexpr int kTelemetryMaxPrevious = 100;
inline constexpr std::size_t kTelemetryTrackPoints = 2000;
inline constexpr std::size_t kTelemetryZipMaxBytes = 32u * 1024u * 1024u;

// ================================================================================

class TelemetryLog {
public:
    explicit TelemetryLog(std::string directory);
    TelemetryLog(std::string directory, std::int64_t start_unix);
    ~TelemetryLog();

    bool ok() const;
    void close();
    void flush() const;
    void await_writes() const;
    void sample(const DogStatus &status, const Config &config);
    void sample_at(const DogStatus &status, const Config &config, std::int64_t unix_s);
    void start_mission();
    void attach(DogStatus &status) const;
    std::uint64_t mission() const;
    std::int64_t mission_start_unix() const;
    doggy::v1::TelemetryTrack track(std::size_t max_points) const;
    bool download_zip(int previous, std::string &bytes, std::string &download_name) const;

private:
    struct Impl;
    Impl *impl_;
};

// ================================================================================

void telemetry_bind(TelemetryLog *log);

// ================================================================================

TelemetryLog *telemetry_current();

// ================================================================================

void telemetry_attach_status(DogStatus &status);

// ================================================================================

std::string telemetry_default_directory();

#endif
