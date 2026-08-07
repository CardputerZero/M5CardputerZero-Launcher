#include "hal_lvgl_bsp.h"
#include "cp0_lvgl_app.h"

#include "cp0_lvgl_log.h"
#include "../cp0_settings_policy.hpp"
#include "../cp0_signal_registration.hpp"
#include "../cp0_sync_signal.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <functional>
#include <limits>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <sys/ioctl.h>
#include <linux/gpio.h>
#include <unistd.h>
#include <utility>

namespace {

struct ExtPortGpioMap {
    const char *chip_path;
    unsigned int grove5v_line;
    unsigned int ext5v_line;
    bool grove5v_active_low;
    bool ext5v_active_low;
};

static constexpr const char *kHwIdPath = "/proc/cardputerzero_hw_id";
static constexpr const char *kGpioDebugPath = "/sys/kernel/debug/gpio";
static constexpr const char *kBacklightControllerName = "m5ioe1";
static constexpr unsigned int kBacklightLine = 9;
static constexpr ExtPortGpioMap kCardputerZeroGpioMap = {"/dev/gpiochip1", 3, 12, false, false};
static constexpr ExtPortGpioMap kFallbackGpioMap = {"/dev/gpiochip0", 17, 5, false, true};
static constexpr const char *kGrove5vLedPaths[] = {
    "/sys/class/leds/grove_5v_out/brightness",
};
static constexpr const char *kExt5vLedPaths[] = {
    "/sys/class/leds/ext_5v_out/brightness",
};

static int gpio_v2_get_value(int line_fd);

static std::string backlight_chip_path()
{
    FILE *file = std::fopen(kGpioDebugPath, "r");
    if (!file) return {};
    char line[512];
    std::string path;
    while (std::fgets(line, sizeof(line), file)) {
        path = cp0::settings::gpiochip_path_from_debug_line(line, kBacklightControllerName);
        if (!path.empty()) break;
    }
    std::fclose(file);
    return path;
}

static int gpio_v2_request_output(const char *chip_path, unsigned int line, const char *consumer, int value)
{
#if defined(GPIO_V2_GET_LINE_IOCTL) && defined(GPIO_V2_LINE_SET_VALUES_IOCTL)
    int chip_fd = open(chip_path, O_RDONLY | O_CLOEXEC);
    if (chip_fd < 0)
        return -errno;

    struct gpio_v2_line_request req;
    std::memset(&req, 0, sizeof(req));
    req.offsets[0] = line;
    req.num_lines = 1;
    std::snprintf(req.consumer, sizeof(req.consumer), "%s", consumer ? consumer : "applaunch");
    req.config.flags = GPIO_V2_LINE_FLAG_OUTPUT;

    if (ioctl(chip_fd, GPIO_V2_GET_LINE_IOCTL, &req) < 0) {
        int err = errno;
        close(chip_fd);
        return -err;
    }
    close(chip_fd);

    struct gpio_v2_line_values vals;
    std::memset(&vals, 0, sizeof(vals));
    vals.mask = 1;
    vals.bits = value ? 1 : 0;
    if (ioctl(req.fd, GPIO_V2_LINE_SET_VALUES_IOCTL, &vals) < 0) {
        int err = errno;
        close(req.fd);
        return -err;
    }
    return req.fd;
#else
    (void)chip_path;
    (void)line;
    (void)consumer;
    (void)value;
    return -ENOSYS;
#endif
}

static int gpio_v2_read_input(const char *chip_path, unsigned int line, const char *consumer)
{
#if defined(GPIO_V2_GET_LINE_IOCTL) && defined(GPIO_V2_LINE_GET_VALUES_IOCTL)
    int chip_fd = open(chip_path, O_RDONLY | O_CLOEXEC);
    if (chip_fd < 0)
        return -errno;

    struct gpio_v2_line_request req;
    std::memset(&req, 0, sizeof(req));
    req.offsets[0] = line;
    req.num_lines = 1;
    std::snprintf(req.consumer, sizeof(req.consumer), "%s", consumer ? consumer : "applaunch");
    req.config.flags = GPIO_V2_LINE_FLAG_INPUT;

    if (ioctl(chip_fd, GPIO_V2_GET_LINE_IOCTL, &req) < 0) {
        int err = errno;
        close(chip_fd);
        return -err;
    }
    close(chip_fd);

    int value = gpio_v2_get_value(req.fd);
    close(req.fd);
    return value;
#else
    (void)chip_path;
    (void)line;
    (void)consumer;
    return -ENOSYS;
#endif
}

static int gpio_v2_get_value(int line_fd)
{
#if defined(GPIO_V2_LINE_GET_VALUES_IOCTL)
    if (line_fd < 0)
        return -EBADF;
    struct gpio_v2_line_values vals;
    std::memset(&vals, 0, sizeof(vals));
    vals.mask = 1;
    if (ioctl(line_fd, GPIO_V2_LINE_GET_VALUES_IOCTL, &vals) < 0)
        return -errno;
    return (vals.bits & 1) ? 1 : 0;
#else
    (void)line_fd;
    return -ENOSYS;
#endif
}

class SettingsSystem
{
public:
    using callback_t = std::function<void(int, std::string)>;
    using arg_t = cp0::settings::Arguments;

    void api_call(arg_t arg, callback_t callback)
    {
        try {
        if (!cp0::settings::valid_arguments(arg)) {
            report(callback, -1, "invalid settings api request");
            return;
        }
        switch (cp0::settings::command_from(arg)) {
        case cp0::settings::Command::BacklightRead: {
            int val = backlight_read();
            report(callback, val < 0 ? -1 : 0, std::to_string(val));
            break;
        }
        case cp0::settings::Command::BacklightMax: {
            int val = backlight_max();
            report(callback, val < 0 ? -1 : 0, std::to_string(val));
            break;
        }
        case cp0::settings::Command::BacklightWrite: {
            int requested = 0;
            if (!cp0::settings::integer_argument(
                    arg, 1, 0, std::numeric_limits<int>::max(), requested)) {
                report(callback, -1, "invalid settings api request");
                break;
            }
            int val = backlight_write(requested);
            report(callback, val < 0 ? -1 : 0, std::to_string(val));
            break;
        }
        case cp0::settings::Command::Log: {
            const std::string topic = cp0::settings::argument_at(arg, 1);
            const std::string message = cp0::settings::argument_at(arg, 2);
            settings_log(topic.c_str(), message.c_str());
            report(callback, 0, "");
            break;
        }
        case cp0::settings::Command::TimeStr: {
            char buf[32] = {};
            time_str(buf, sizeof(buf));
            report(callback, 0, buf);
            break;
        }
        case cp0::settings::Command::GpioSet: {
            const auto output = cp0::settings::power_output_from_name(cp0::settings::argument_at(arg, 1));
            int requested = 0;
            if (!cp0::settings::integer_argument(arg, 2, 0, 1, requested)) {
                report(callback, -1, "invalid settings api request");
                break;
            }
            int ret = set_named_gpio(output, requested);
            report(callback, ret, ret == 0 ? "ok" : std::string("gpio set failed: ") + std::to_string(-ret));
            break;
        }
        case cp0::settings::Command::GpioGet: {
            int val = get_named_gpio(cp0::settings::power_output_from_name(cp0::settings::argument_at(arg, 1)));
            if (val < 0)
                report(callback, val, std::string("gpio get failed: ") + std::to_string(-val));
            else
                report(callback, 0, std::to_string(val));
            break;
        }
        case cp0::settings::Command::Unknown:
        default:
            report(callback, -1, "unknown settings api command");
            break;
        }
        } catch (...) {
            report(callback, -1, "settings api failure");
        }
    }

    static int api_int(const arg_t &arg,
                       int default_value,
                       int minimum,
                       int maximum)
    {
        int result = default_value;
        cp0::signal::invoke_noexcept([&] { cp0_signal_settings_api(arg, [&](int code, std::string data) {
            int parsed = 0;
            if (code == 0 &&
                cp0::settings::parse_integer_response(data, minimum, maximum, parsed))
                result = parsed;
        }); });
        return result;
    }

    static void api_time_str(char *buf, int buf_size)
    {
        if (!buf || buf_size <= 0)
            return;
        buf[0] = '\0';
        cp0::signal::invoke_noexcept([&] { cp0_signal_settings_api({"TimeStr"}, [&](int code, std::string data) {
            if (code == 0)
                copy_string(buf, static_cast<size_t>(buf_size), data);
        }); });
        if (buf[0] == '\0')
            fallback_time_str(buf, buf_size);
    }

private:
    std::mutex gpio_mutex_;
    const ExtPortGpioMap *gpio_map_ = nullptr;

    void report(callback_t callback, int code, const std::string &data)
    {
        if (!callback) return;
        try {
            callback(code, data);
        } catch (...) {
        }
    }

    int set_named_gpio(cp0::settings::PowerOutput output, int val)
    {
        std::lock_guard<std::mutex> lock(gpio_mutex_);
        if (output == cp0::settings::PowerOutput::Grove5V) {
            int ret = write_led_value(kGrove5vLedPaths, val);
            if (ret != -ENOENT)
                return ret;
            const ExtPortGpioMap &map = active_gpio_map_locked();
            return set_gpio_value(map.chip_path, map.grove5v_line, map.grove5v_active_low, "GROVE5V", val);
        }
        if (output == cp0::settings::PowerOutput::Ext5V) {
            int ret = write_led_value(kExt5vLedPaths, val);
            if (ret != -ENOENT)
                return ret;
            const ExtPortGpioMap &map = active_gpio_map_locked();
            return set_gpio_value(map.chip_path, map.ext5v_line, map.ext5v_active_low, "EXT5V", val);
        }
        if (output == cp0::settings::PowerOutput::Backlight) {
            const std::string chip_path = backlight_chip_path();
            return chip_path.empty()
                ? -ENOENT
                : set_gpio_value(chip_path.c_str(), kBacklightLine, false, "BACKLIGHT", val);
        }
        return -EINVAL;
    }

    int get_named_gpio(cp0::settings::PowerOutput output)
    {
        std::lock_guard<std::mutex> lock(gpio_mutex_);
        if (output == cp0::settings::PowerOutput::Grove5V) {
            int value = read_led_value(kGrove5vLedPaths);
            if (value != -ENOENT)
                return value;
            const ExtPortGpioMap &map = active_gpio_map_locked();
            return get_gpio_value(map.chip_path, map.grove5v_line, map.grove5v_active_low, "GROVE5V");
        }
        if (output == cp0::settings::PowerOutput::Ext5V) {
            int value = read_led_value(kExt5vLedPaths);
            if (value != -ENOENT)
                return value;
            const ExtPortGpioMap &map = active_gpio_map_locked();
            return get_gpio_value(map.chip_path, map.ext5v_line, map.ext5v_active_low, "EXT5V");
        }
        if (output == cp0::settings::PowerOutput::Backlight) {
            const std::string chip_path = backlight_chip_path();
            return chip_path.empty()
                ? -ENOENT
                : get_gpio_value(chip_path.c_str(), kBacklightLine, false, "BACKLIGHT");
        }
        return -EINVAL;
    }

    const ExtPortGpioMap &active_gpio_map_locked()
    {
        const ExtPortGpioMap *map = access(kHwIdPath, F_OK) == 0 ? &kCardputerZeroGpioMap : &kFallbackGpioMap;
        if (gpio_map_ != map)
            gpio_map_ = map;
        return *gpio_map_;
    }

    int set_gpio_value(const char *chip_path, unsigned int line, bool active_low, const char *consumer, int val)
    {
        int bit = cp0::settings::physical_line_value(val, active_low);
        int fd = gpio_v2_request_output(chip_path, line, consumer, bit);
        if (fd < 0)
            return fd;
        close(fd);
        return fd >= 0 ? 0 : fd;
    }

    int get_gpio_value(const char *chip_path, unsigned int line, bool active_low, const char *consumer)
    {
        int value = gpio_v2_read_input(chip_path, line, consumer);
        return value < 0 ? value : cp0::settings::logical_line_value(value, active_low);
    }

    template <size_t N>
    static int write_led_value(const char *const (&paths)[N], int val)
    {
        const char value = cp0::settings::switch_value(val) ? '1' : '0';
        for (const char *path : paths) {
            int fd = open(path, O_WRONLY | O_CLOEXEC);
            if (fd < 0) {
                if (errno == ENOENT)
                    continue;
                return -errno;
            }
            ssize_t written = write(fd, &value, 1);
            int err = written == 1 ? 0 : (written < 0 ? errno : EIO);
            close(fd);
            return -err;
        }
        return -ENOENT;
    }

    template <size_t N>
    static int read_led_value(const char *const (&paths)[N])
    {
        for (const char *path : paths) {
            int fd = open(path, O_RDONLY | O_CLOEXEC);
            if (fd < 0) {
                if (errno == ENOENT)
                    continue;
                return -errno;
            }
            char buf[16] = {};
            ssize_t size = read(fd, buf, sizeof(buf) - 1);
            int err = size >= 0 ? 0 : errno;
            close(fd);
            if (err != 0)
                return -err;
            if (size == 0)
                return -EIO;
            int value = 0;
            return cp0::settings::parse_integer_file_line(
                       std::string_view(buf, static_cast<std::size_t>(size)), 0,
                       std::numeric_limits<int>::max(), value)
                       ? cp0::settings::switch_value(value)
                       : -EINVAL;
        }
        return -ENOENT;
    }

    static void copy_string(char *dst, size_t dst_size, const std::string &src)
    {
        if (!dst || dst_size == 0)
            return;
        std::strncpy(dst, src.c_str(), dst_size - 1);
        dst[dst_size - 1] = '\0';
    }

    static void fallback_time_str(char *buf, int buf_size)
    {
        if (!buf || buf_size <= 0)
            return;
        std::time_t now = std::time(nullptr);
        std::tm *t = std::localtime(&now);
        if (!t) {
            buf[0] = '\0';
            return;
        }
        std::snprintf(buf, static_cast<size_t>(buf_size), "%02d:%02d", t->tm_hour, t->tm_min);
    }

    static int read_int_file(const char *path,
                             int default_value,
                             int minimum,
                             int maximum)
    {
        FILE *f = std::fopen(path, "r");
        if (!f)
            return default_value;
        char buffer[64] = {};
        const bool read_succeeded = std::fgets(buffer, sizeof(buffer), f) != nullptr;
        std::fclose(f);
        int value = default_value;
        return read_succeeded && cp0::settings::parse_integer_file_line(
                                     buffer, minimum, maximum, value)
                   ? value
                   : default_value;
    }

    int backlight_read()
    {
        return read_int_file("/sys/class/backlight/backlight/brightness", -1, 0,
                             std::numeric_limits<int>::max());
    }

    int backlight_max()
    {
        return read_int_file("/sys/class/backlight/backlight/max_brightness", 100, 1,
                             std::numeric_limits<int>::max());
    }

    int backlight_write(int val)
    {
        if (val < 0)
            val = 0;
        int mx = backlight_max();
        if (val > mx)
            val = mx;
        FILE *f = std::fopen("/sys/class/backlight/backlight/brightness", "w");
        if (!f)
            return -1;
        const int written = std::fprintf(f, "%d", val);
        const int close_result = std::fclose(f);
        return written > 0 && close_result == 0 ? val : -1;
    }

    void settings_log(const char *topic, const char *message)
    {
        cp0_zmq_log(topic && topic[0] ? topic : "settings", message ? message : "");
    }

    void time_str(char *buf, int buf_size) { fallback_time_str(buf, buf_size); }
};

} // namespace

extern "C" void init_settings(void)
{
    cp0_zmq_log_init();
    cp0_zmq_log("settings", "init_settings");
    auto settings = std::make_shared<SettingsSystem>();
    static cp0::SignalRegistration<decltype(cp0_signal_settings_api)> registration;
    registration.replace(cp0_signal_settings_api,
                         [settings](std::list<std::string> arg,
                                    std::function<void(int, std::string)> callback) {
                             settings->api_call(std::move(arg), std::move(callback));
                         });
}

extern "C" int cp0_backlight_read(void)
{
    return SettingsSystem::api_int(
        {"BacklightRead"}, -1, 0, std::numeric_limits<int>::max());
}

extern "C" int cp0_backlight_max(void)
{
    return SettingsSystem::api_int(
        {"BacklightMax"}, 100, 1, std::numeric_limits<int>::max());
}

extern "C" int cp0_backlight_write(int val)
{
    if (val < 0) return -1;
    return SettingsSystem::api_int(
        {"BacklightWrite", std::to_string(val)}, -1, 0,
        std::numeric_limits<int>::max());
}

extern "C" void cp0_time_str(char *buf, int buf_size)
{
    SettingsSystem::api_time_str(buf, buf_size);
}
