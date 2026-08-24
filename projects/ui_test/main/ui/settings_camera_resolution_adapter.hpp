#pragma once

#include <array>
#include <charconv>
#include <cctype>
#include <climits>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

namespace settings_camera_resolution {

inline constexpr char kWidthKey[]  = "camera.resolution.width";
inline constexpr char kHeightKey[] = "camera.resolution.height";

struct Resolution {
    int width;
    int height;

    friend constexpr bool operator==(const Resolution &left, const Resolution &right)
    {
        return left.width == right.width && left.height == right.height;
    }

    friend constexpr bool operator!=(const Resolution &left, const Resolution &right)
    {
        return !(left == right);
    }
};

inline constexpr Resolution kDefaultResolution{1280, 720};
inline constexpr std::array<Resolution, 2> kSupportedResolutions{{
    {1280, 720},
    {640, 480},
}};

using ConfigArguments = std::list<std::string>;
using ConfigCallback  = std::function<void(int, std::string)>;
using ConfigInvoker   = std::function<void(ConfigArguments, ConfigCallback)>;

enum class ReadStatus {
    Ok,
    Defaulted,
    BackendError,
};

struct ReadResult {
    ReadStatus status = ReadStatus::BackendError;
    Resolution resolution = kDefaultResolution;
    std::string message;

    constexpr bool usable() const
    {
        return status == ReadStatus::Ok || status == ReadStatus::Defaulted;
    }

    constexpr bool defaulted() const
    {
        return status == ReadStatus::Defaulted;
    }
};

enum class WriteStatus {
    Ok,
    InvalidTarget,
    ReadFailed,
    WidthWriteFailed,
    HeightWriteFailed,
    SaveFailed,
    RollbackFailed,
};

struct WriteResult {
    WriteStatus status = WriteStatus::ReadFailed;
    Resolution previous = kDefaultResolution;
    bool previous_defaulted = false;
    bool rollback_attempted = false;
    bool rollback_succeeded = false;
    std::string message;

    constexpr bool succeeded() const
    {
        return status == WriteStatus::Ok;
    }
};

inline bool parse_integer(std::string_view text, int &value)
{
    std::size_t begin_index = 0;
    while (begin_index < text.size() &&
           std::isspace(static_cast<unsigned char>(text[begin_index]))) {
        ++begin_index;
    }
    if (begin_index == text.size()) return false;

    bool has_plus = false;
    if (text[begin_index] == '+') {
        has_plus = true;
        ++begin_index;
    }
    if (begin_index == text.size()) return false;

    int parsed = 0;
    const char *begin = text.data() + begin_index;
    const char *end   = text.data() + text.size();
    const auto result = std::from_chars(begin, end, parsed, 10);
    if (result.ec != std::errc{} || result.ptr != end) return false;
    if (has_plus && parsed < 0) return false;

    value = parsed;
    return true;
}

inline bool is_supported(Resolution resolution)
{
    for (const Resolution option : kSupportedResolutions) {
        if (option == resolution) return true;
    }
    return false;
}

inline int index_for_resolution(Resolution resolution)
{
    for (std::size_t index = 0; index < kSupportedResolutions.size(); ++index) {
        if (kSupportedResolutions[index] == resolution)
            return static_cast<int>(index);
    }
    return -1;
}

inline bool resolution_for_index(int index, Resolution &resolution)
{
    if (index < 0 || index >= static_cast<int>(kSupportedResolutions.size())) return false;
    resolution = kSupportedResolutions[static_cast<std::size_t>(index)];
    return true;
}

namespace detail {

struct ConfigResponseState {
    std::mutex mutex;
    bool called = false;
    int code = -1;
    std::string data;
};

struct ConfigResponse {
    bool called = false;
    int code = -1;
    std::string data;
};

inline ConfigResponse invoke_config(const ConfigInvoker &invoker, ConfigArguments arguments)
{
    auto state = std::make_shared<ConfigResponseState>();
    if (!invoker) return {};

    try {
        invoker(std::move(arguments), [state](int code, std::string data) {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (state->called) return;
            state->called = true;
            state->code = code;
            state->data = std::move(data);
        });
    } catch (...) {
        return {};
    }

    std::lock_guard<std::mutex> lock(state->mutex);
    return {state->called, state->code, state->data};
}

inline bool response_ok(const ConfigResponse &response)
{
    return response.called && response.code == 0 && !response.data.empty();
}

inline ReadResult backend_error(const char *message)
{
    return {ReadStatus::BackendError, kDefaultResolution, message};
}

inline ReadResult defaulted_result(const char *message)
{
    return {ReadStatus::Defaulted, kDefaultResolution, message};
}

inline ConfigResponse get_int(const ConfigInvoker &invoker, const char *key, int fallback)
{
    return invoke_config(invoker, {"GetInt", key, std::to_string(fallback)});
}

inline ConfigResponse set_int(const ConfigInvoker &invoker, const char *key, int value)
{
    return invoke_config(invoker, {"SetInt", key, std::to_string(value)});
}

inline ConfigResponse save(const ConfigInvoker &invoker)
{
    return invoke_config(invoker, {"Save"});
}

inline bool rollback(const ConfigInvoker &invoker, Resolution resolution)
{
    const bool width_restored = response_ok(set_int(invoker, kWidthKey, resolution.width));
    const bool height_restored = response_ok(set_int(invoker, kHeightKey, resolution.height));
    const bool saved = response_ok(save(invoker));
    return width_restored && height_restored && saved;
}

} // namespace detail

inline ReadResult read_resolution(const ConfigInvoker &invoker)
{
    const auto width_response = detail::get_int(invoker, kWidthKey, kDefaultResolution.width);
    const auto height_response = detail::get_int(invoker, kHeightKey, kDefaultResolution.height);
    if (!detail::response_ok(width_response))
        return detail::backend_error("camera width read failed");
    if (!detail::response_ok(height_response))
        return detail::backend_error("camera height read failed");

    int width = 0;
    int height = 0;
    if (!parse_integer(width_response.data, width))
        return detail::defaulted_result("camera width is invalid; using 1280x720");
    if (!parse_integer(height_response.data, height))
        return detail::defaulted_result("camera height is invalid; using 1280x720");

    const Resolution resolution{width, height};
    if (!is_supported(resolution))
        return detail::defaulted_result("camera resolution is unsupported; using 1280x720");

    return {ReadStatus::Ok, resolution, {}};
}

inline WriteResult write_resolution(const ConfigInvoker &invoker, Resolution target)
{
    WriteResult result;
    if (!is_supported(target)) {
        result.status = WriteStatus::InvalidTarget;
        result.message = "camera resolution is unsupported";
        return result;
    }

    const ReadResult previous = read_resolution(invoker);
    if (!previous.usable()) {
        result.status = WriteStatus::ReadFailed;
        result.message = previous.message.empty() ? "camera resolution read failed" : previous.message;
        return result;
    }
    result.previous = previous.resolution;
    result.previous_defaulted = previous.defaulted();

    const auto width_response = detail::set_int(invoker, kWidthKey, target.width);
    if (!detail::response_ok(width_response)) {
        result.status = WriteStatus::WidthWriteFailed;
        result.message = "camera width write failed";
    } else {
        const auto height_response = detail::set_int(invoker, kHeightKey, target.height);
        if (!detail::response_ok(height_response)) {
            result.status = WriteStatus::HeightWriteFailed;
            result.message = "camera height write failed";
        } else {
            const auto save_response = detail::save(invoker);
            if (!detail::response_ok(save_response)) {
                result.status = WriteStatus::SaveFailed;
                result.message = "camera resolution save failed";
            } else {
                result.status = WriteStatus::Ok;
                result.message = "camera resolution saved";
                return result;
            }
        }
    }

    result.rollback_attempted = true;
    result.rollback_succeeded = detail::rollback(invoker, result.previous);
    if (!result.rollback_succeeded) {
        result.status = WriteStatus::RollbackFailed;
        result.message = "camera resolution rollback failed";
    }
    return result;
}

} // namespace settings_camera_resolution
