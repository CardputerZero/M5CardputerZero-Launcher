#pragma once

#if __has_include("model/setup_value_policy.hpp")
#include "model/setup_value_policy.hpp"
#elif __has_include("../../../APPLaunch/main/ui/model/setup_value_policy.hpp")
#include "../../../APPLaunch/main/ui/model/setup_value_policy.hpp"
#else
#error "setup_value_policy.hpp is required"
#endif

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <climits>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

namespace settings_screen {

inline constexpr char kBrightnessKey[] = "brightness";
inline constexpr char kDarkTimeKey[]   = "dark_time";
inline constexpr int kDefaultDarkTime = 30;
inline constexpr int kLegacyBrightnessOptionCount = 4;
inline constexpr int kZeroBrightnessOptionCount   = 5;
inline constexpr std::chrono::milliseconds kApiTimeout{3000};

using Arguments = std::list<std::string>;
using Callback  = std::function<void(int, std::string)>;
using SettingsInvoker = std::function<void(Arguments, Callback)>;
using ConfigInvoker   = std::function<void(Arguments, Callback)>;

enum class ResponseStatus : uint8_t {
    Ok,
    InvokerUnavailable,
    CallbackMissing,
    Timeout,
    BackendError,
    InvalidPayload,
};

struct Response {
    ResponseStatus status = ResponseStatus::InvokerUnavailable;
    int code = -1;
    std::string data;

    bool succeeded() const noexcept
    {
        return status == ResponseStatus::Ok && code == 0;
    }
};

inline bool parse_nonnegative(std::string_view text, int &value)
{
    return setup_values::parse_nonnegative_int(text, value);
}

inline bool parse_bounded(const Response &response, int minimum, int maximum, int &value)
{
    if (!response.succeeded()) return false;
    if (!parse_nonnegative(response.data, value)) return false;
    return value >= minimum && value <= maximum;
}

inline bool response_is_ok(const Response &response)
{
    return response.succeeded() && response.data == "ok";
}

namespace detail {

struct WaitState {
    std::mutex mutex;
    std::condition_variable condition;
    bool completed = false;
    int code = -1;
    std::string data;
};

template <typename Invoker>
inline Response invoke(const Arguments &arguments,
                       const Invoker &invoker,
                       std::chrono::milliseconds timeout = kApiTimeout)
{
    if (!invoker)
        return {ResponseStatus::InvokerUnavailable, -1, "screen settings invoker unavailable"};

    std::shared_ptr<WaitState> state;
    try {
        state = std::make_shared<WaitState>();
        invoker(arguments, [state](int code, std::string data) {
            try {
                std::lock_guard<std::mutex> lock(state->mutex);
                if (state->completed) return;
                state->code = code;
                state->data = std::move(data);
                state->completed = true;
                state->condition.notify_one();
            } catch (...) {
            }
        });
    } catch (...) {
        return {ResponseStatus::BackendError, -1, "screen settings invocation failed"};
    }

    std::unique_lock<std::mutex> lock(state->mutex);
    if (!state->condition.wait_for(lock, timeout, [state] { return state->completed; }))
        return {ResponseStatus::Timeout, -1, "screen settings api timeout"};
    if (state->code != 0)
        return {ResponseStatus::BackendError, state->code, std::move(state->data)};
    return {ResponseStatus::Ok, state->code, std::move(state->data)};
}

inline Response get_int(const ConfigInvoker &invoker,
                        const char *key,
                        int fallback,
                        std::chrono::milliseconds timeout)
{
    return invoke({"GetInt", key, std::to_string(fallback)}, invoker, timeout);
}

inline Response set_int(const ConfigInvoker &invoker,
                        const char *key,
                        int value,
                        std::chrono::milliseconds timeout)
{
    return invoke({"SetInt", key, std::to_string(value)}, invoker, timeout);
}

inline Response save(const ConfigInvoker &invoker, std::chrono::milliseconds timeout)
{
    return invoke({"Save"}, invoker, timeout);
}

inline bool restore_config(const ConfigInvoker &invoker,
                           int value,
                           std::chrono::milliseconds timeout)
{
    const Response set_response = set_int(invoker, kBrightnessKey, value, timeout);
    const Response save_response = save(invoker, timeout);
    return response_is_ok(set_response) && response_is_ok(save_response);
}

inline bool restore_dark_time(const ConfigInvoker &invoker,
                              int value,
                              std::chrono::milliseconds timeout)
{
    const Response set_response = set_int(invoker, kDarkTimeKey, value, timeout);
    const Response save_response = save(invoker, timeout);
    return response_is_ok(set_response) && response_is_ok(save_response);
}

}

inline Response invoke_settings(const Arguments &arguments,
                                const SettingsInvoker &invoker,
                                std::chrono::milliseconds timeout = kApiTimeout)
{
    return detail::invoke(arguments, invoker, timeout);
}

inline Response invoke_config(const Arguments &arguments,
                              const ConfigInvoker &invoker,
                              std::chrono::milliseconds timeout = kApiTimeout)
{
    return detail::invoke(arguments, invoker, timeout);
}

inline int normalized_brightness_option_count(int option_count)
{
    return option_count >= kZeroBrightnessOptionCount
               ? kZeroBrightnessOptionCount
               : kLegacyBrightnessOptionCount;
}

inline int brightness_percent(int index, int option_count = kZeroBrightnessOptionCount)
{
    static constexpr std::array<int, kLegacyBrightnessOptionCount> legacy_percentages{{100, 75, 50, 25}};
    static constexpr std::array<int, kZeroBrightnessOptionCount> zero_percentages{{100, 75, 50, 25, 0}};
    const int normalized_count = normalized_brightness_option_count(option_count);
    if (normalized_count == kZeroBrightnessOptionCount) {
        const int safe_index = std::clamp(index, 0, kZeroBrightnessOptionCount - 1);
        return zero_percentages[static_cast<std::size_t>(safe_index)];
    }
    const int safe_index = std::clamp(index, 0, kLegacyBrightnessOptionCount - 1);
    return legacy_percentages[static_cast<std::size_t>(safe_index)];
}

inline int brightness_index(int value,
                            int maximum,
                            int option_count = kZeroBrightnessOptionCount)
{
    if (normalized_brightness_option_count(option_count) == kLegacyBrightnessOptionCount)
        return setup_values::brightness_index(value, maximum);
    if (maximum <= 0) return 0;

    const int percent = static_cast<int>(static_cast<std::int64_t>(value) * 100 / maximum);
    if (percent >= 88) return 0;
    if (percent >= 63) return 1;
    if (percent >= 38) return 2;
    if (percent >= 13) return 3;
    return 4;
}

inline int brightness_value(int index,
                            int maximum,
                            int option_count = kZeroBrightnessOptionCount)
{
    const int safe_maximum = std::max(1, maximum);
    if (normalized_brightness_option_count(option_count) == kLegacyBrightnessOptionCount)
        return setup_values::brightness_value(index, safe_maximum);

    const int percentage = brightness_percent(index, kZeroBrightnessOptionCount);
    return static_cast<int>(static_cast<std::int64_t>(safe_maximum) * percentage / 100);
}

inline bool brightness_value_valid(int value, int maximum)
{
    return maximum > 0 && value >= 0 && value <= maximum;
}

inline bool brightness_index_valid(int index, int option_count = kZeroBrightnessOptionCount)
{
    const int count = normalized_brightness_option_count(option_count);
    return index >= 0 && index < count;
}

inline int dark_time_index(int seconds)
{
    return setup_values::dark_time_index(seconds);
}

inline int dark_time_seconds(int index)
{
    return setup_values::dark_time_seconds(index);
}

inline bool dark_time_index_valid(int index)
{
    return index >= 0 && index < 5;
}

enum class BrightnessReadStatus : uint8_t {
    Ok,
    Defaulted,
    BackendError,
    InvalidPayload,
};

struct BrightnessReadResult {
    BrightnessReadStatus status = BrightnessReadStatus::BackendError;
    int maximum = 100;
    int value = 75;
    int index = 0;
    std::string message;

    bool usable() const noexcept
    {
        return status == BrightnessReadStatus::Ok || status == BrightnessReadStatus::Defaulted;
    }

    bool defaulted() const noexcept
    {
        return status == BrightnessReadStatus::Defaulted;
    }
};

inline BrightnessReadResult read_brightness(
    const SettingsInvoker &settings_invoker,
    const ConfigInvoker &config_invoker,
    int option_count = kZeroBrightnessOptionCount,
    std::chrono::milliseconds timeout = kApiTimeout)
{
    const Response maximum_response = detail::invoke({"BacklightMax"}, settings_invoker, timeout);
    int maximum = 0;
    if (!parse_bounded(maximum_response, 1, INT_MAX, maximum)) {
        return {maximum_response.status == ResponseStatus::Ok
                    ? BrightnessReadStatus::InvalidPayload
                    : BrightnessReadStatus::BackendError,
                100,
                75,
                brightness_index(75, 100, option_count),
                "backlight maximum read failed"};
    }

    const Response value_response = detail::invoke({"BacklightRead"}, settings_invoker, timeout);
    int value = 0;
    if (parse_bounded(value_response, 0, maximum, value)) {
        return {BrightnessReadStatus::Ok,
                maximum,
                value,
                brightness_index(value, maximum, option_count),
                {}};
    }

    const Response config_response = detail::get_int(config_invoker, kBrightnessKey, maximum, timeout);
    int saved_value = 0;
    if (!parse_bounded(config_response, 0, maximum, saved_value)) {
        return {config_response.status == ResponseStatus::Ok
                    ? BrightnessReadStatus::InvalidPayload
                    : BrightnessReadStatus::BackendError,
                maximum,
                maximum,
                brightness_index(maximum, maximum, option_count),
                "backlight and saved brightness read failed"};
    }

    return {BrightnessReadStatus::Defaulted,
            maximum,
            saved_value,
            brightness_index(saved_value, maximum, option_count),
            "backlight read failed; using saved brightness"};
}

enum class BrightnessWriteStatus : uint8_t {
    Ok,
    InvalidTarget,
    ConfigReadFailed,
    BacklightWriteFailed,
    BacklightPayloadInvalid,
    ConfigWriteFailed,
    SaveFailed,
    RollbackFailed,
};

struct BrightnessWriteResult {
    BrightnessWriteStatus status = BrightnessWriteStatus::ConfigReadFailed;
    int previous_value = 0;
    int previous_config = 0;
    int applied_value = 0;
    int applied_index = 0;
    bool rollback_attempted = false;
    bool rollback_succeeded = false;
    std::string message;

    bool succeeded() const noexcept
    {
        return status == BrightnessWriteStatus::Ok;
    }
};

inline bool restore_brightness(
    const SettingsInvoker &settings_invoker,
    const ConfigInvoker &config_invoker,
    int value,
    int maximum,
    int config_value,
    std::chrono::milliseconds timeout)
{
    const Response hardware_response = detail::invoke(
        {"BacklightWrite", std::to_string(value)}, settings_invoker, timeout);
    int restored_value = -1;
    const bool hardware_restored = parse_bounded(hardware_response, 0, maximum, restored_value) &&
                                   restored_value == value;
    const bool config_restored = detail::restore_config(config_invoker, config_value, timeout);
    return hardware_restored && config_restored;
}

inline BrightnessWriteResult write_brightness(
    const SettingsInvoker &settings_invoker,
    const ConfigInvoker &config_invoker,
    int index,
    int maximum,
    int previous_value,
    int option_count = kZeroBrightnessOptionCount,
    std::chrono::milliseconds timeout = kApiTimeout)
{
    BrightnessWriteResult result;
    result.previous_value = previous_value;
    result.previous_config = previous_value;
    if (!brightness_value_valid(previous_value, maximum) ||
        !brightness_index_valid(index, option_count)) {
        result.status = BrightnessWriteStatus::InvalidTarget;
        result.message = "invalid brightness value";
        return result;
    }

    const Response config_response = detail::get_int(
        config_invoker, kBrightnessKey, previous_value, timeout);
    if (!parse_bounded(config_response, 0, maximum, result.previous_config)) {
        result.status = BrightnessWriteStatus::ConfigReadFailed;
        result.message = "brightness config read failed";
        return result;
    }

    const int requested_value = brightness_value(index, maximum, option_count);
    if (!brightness_value_valid(requested_value, maximum)) {
        result.status = BrightnessWriteStatus::InvalidTarget;
        result.message = "invalid brightness target";
        return result;
    }

    const Response hardware_response = detail::invoke(
        {"BacklightWrite", std::to_string(requested_value)}, settings_invoker, timeout);
    int applied_value = 0;
    if (!hardware_response.succeeded()) {
        result.status = BrightnessWriteStatus::BacklightWriteFailed;
        result.message = "backlight write failed";
        return result;
    }
    if (!parse_bounded(hardware_response, 0, maximum, applied_value)) {
        result.status = BrightnessWriteStatus::BacklightPayloadInvalid;
        result.rollback_attempted = true;
        result.rollback_succeeded = restore_brightness(
            settings_invoker, config_invoker, previous_value, maximum,
            result.previous_config, timeout);
        if (!result.rollback_succeeded) result.status = BrightnessWriteStatus::RollbackFailed;
        result.message = result.rollback_succeeded
                             ? "invalid backlight write response"
                             : "brightness rollback failed";
        return result;
    }

    result.applied_value = applied_value;
    result.applied_index = brightness_index(applied_value, maximum, option_count);
    const Response set_response = detail::set_int(
        config_invoker, kBrightnessKey, applied_value, timeout);
    if (!response_is_ok(set_response)) {
        result.status = BrightnessWriteStatus::ConfigWriteFailed;
        result.rollback_attempted = true;
        result.rollback_succeeded = restore_brightness(
            settings_invoker, config_invoker, previous_value, maximum,
            result.previous_config, timeout);
        if (!result.rollback_succeeded) result.status = BrightnessWriteStatus::RollbackFailed;
        result.message = result.rollback_succeeded
                             ? "brightness config write failed"
                             : "brightness rollback failed";
        return result;
    }

    const Response save_response = detail::save(config_invoker, timeout);
    if (!response_is_ok(save_response)) {
        result.status = BrightnessWriteStatus::SaveFailed;
        result.rollback_attempted = true;
        result.rollback_succeeded = restore_brightness(
            settings_invoker, config_invoker, previous_value, maximum,
            result.previous_config, timeout);
        if (!result.rollback_succeeded) result.status = BrightnessWriteStatus::RollbackFailed;
        result.message = result.rollback_succeeded
                             ? "brightness save failed"
                             : "brightness rollback failed";
        return result;
    }

    result.status = BrightnessWriteStatus::Ok;
    result.message = "brightness saved";
    return result;
}

enum class DarkTimeReadStatus : uint8_t {
    Ok,
    Defaulted,
    BackendError,
    InvalidPayload,
};

struct DarkTimeReadResult {
    DarkTimeReadStatus status = DarkTimeReadStatus::BackendError;
    int seconds = kDefaultDarkTime;
    int index = 2;
    std::string message;

    bool usable() const noexcept
    {
        return status == DarkTimeReadStatus::Ok || status == DarkTimeReadStatus::Defaulted;
    }

    bool defaulted() const noexcept
    {
        return status == DarkTimeReadStatus::Defaulted;
    }
};

inline DarkTimeReadResult read_dark_time(
    const ConfigInvoker &config_invoker,
    std::chrono::milliseconds timeout = kApiTimeout)
{
    const Response response = detail::get_int(config_invoker, kDarkTimeKey, kDefaultDarkTime, timeout);
    int seconds = 0;
    if (!response.succeeded())
        return {DarkTimeReadStatus::BackendError, kDefaultDarkTime, 2, "dark time read failed"};
    if (!parse_nonnegative(response.data, seconds))
        return {DarkTimeReadStatus::Defaulted,
                kDefaultDarkTime,
                dark_time_index(kDefaultDarkTime),
                "invalid dark time; using 30S"};

    const int index = dark_time_index(seconds);
    const int normalized_seconds = dark_time_seconds(index);
    if (normalized_seconds != seconds)
        return {DarkTimeReadStatus::Defaulted,
                normalized_seconds,
                index,
                "unsupported dark time; using 30S"};
    return {DarkTimeReadStatus::Ok, seconds, index, {}};
}

enum class DarkTimeWriteStatus : uint8_t {
    Ok,
    InvalidTarget,
    ReadFailed,
    SetFailed,
    SaveFailed,
    RollbackFailed,
};

struct DarkTimeWriteResult {
    DarkTimeWriteStatus status = DarkTimeWriteStatus::ReadFailed;
    int previous_seconds = kDefaultDarkTime;
    int previous_index = 2;
    int applied_seconds = kDefaultDarkTime;
    bool rollback_attempted = false;
    bool rollback_succeeded = false;
    std::string message;

    bool succeeded() const noexcept
    {
        return status == DarkTimeWriteStatus::Ok;
    }
};

inline DarkTimeWriteResult write_dark_time(
    const ConfigInvoker &config_invoker,
    int index,
    std::chrono::milliseconds timeout = kApiTimeout)
{
    DarkTimeWriteResult result;
    if (!dark_time_index_valid(index)) {
        result.status = DarkTimeWriteStatus::InvalidTarget;
        result.message = "invalid dark time target";
        return result;
    }

    const DarkTimeReadResult previous = read_dark_time(config_invoker, timeout);
    if (!previous.usable()) {
        result.status = DarkTimeWriteStatus::ReadFailed;
        result.message = "dark time read failed";
        return result;
    }
    result.previous_seconds = previous.seconds;
    result.previous_index = previous.index;
    result.applied_seconds = dark_time_seconds(index);

    const Response set_response = detail::set_int(
        config_invoker, kDarkTimeKey, result.applied_seconds, timeout);
    if (!response_is_ok(set_response)) {
        result.status = DarkTimeWriteStatus::SetFailed;
        result.rollback_attempted = true;
        result.rollback_succeeded = detail::restore_dark_time(
            config_invoker, result.previous_seconds, timeout);
        if (!result.rollback_succeeded) result.status = DarkTimeWriteStatus::RollbackFailed;
        result.message = result.rollback_succeeded
                             ? "dark time write failed"
                             : "dark time rollback failed";
        return result;
    }

    const Response save_response = detail::save(config_invoker, timeout);
    if (!response_is_ok(save_response)) {
        result.status = DarkTimeWriteStatus::SaveFailed;
        result.rollback_attempted = true;
        result.rollback_succeeded = detail::restore_dark_time(
            config_invoker, result.previous_seconds, timeout);
        if (!result.rollback_succeeded) result.status = DarkTimeWriteStatus::RollbackFailed;
        result.message = result.rollback_succeeded
                             ? "dark time save failed"
                             : "dark time rollback failed";
        return result;
    }

    result.status = DarkTimeWriteStatus::Ok;
    result.message = "dark time saved";
    return result;
}

}
