#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <string>

class SettingsBatteryApi {
public:
    using Arguments = std::list<std::string>;
    using Callback = std::function<void(int, std::string)>;
    using Dispatch = std::function<void(Arguments, Callback)>;

    SettingsBatteryApi();
    explicit SettingsBatteryApi(Dispatch dispatch);

    void read(Callback callback) const;
    void calibrate(int command_index, Callback callback) const;

    static bool valid_calibration_index(int command_index);
    static Arguments calibration_arguments(int command_index);

private:
    void request(Arguments arguments, Callback callback) const;

    Dispatch dispatch_;
};

enum class SettingsBatteryOperation {
    Read,
    Calibrate,
};

enum class SettingsBatteryOutcome {
    Success,
    Failed,
    TimedOut,
    Cancelled,
};

struct SettingsBatteryOperationResult {
    SettingsBatteryOperation operation = SettingsBatteryOperation::Read;
    SettingsBatteryOutcome outcome = SettingsBatteryOutcome::Failed;
    std::uint64_t generation = 0;
    int calibration_index = -1;
    int code = -1;
    std::string payload;
};

class SettingsBatteryRequestCoordinator {
public:
    using Post = std::function<bool(std::function<void()>)>;
    using Completion = std::function<void(const SettingsBatteryOperationResult &)>;

    SettingsBatteryRequestCoordinator(
        SettingsBatteryApi api,
        Post post,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(1800));
    ~SettingsBatteryRequestCoordinator();

    SettingsBatteryRequestCoordinator(const SettingsBatteryRequestCoordinator &) = delete;
    SettingsBatteryRequestCoordinator &operator=(const SettingsBatteryRequestCoordinator &) = delete;

    bool read(Completion completion);
    bool calibrate(int command_index, Completion completion);
    void cancel();
    void shutdown();

    bool pending() const;
    std::uint64_t generation() const;

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};
