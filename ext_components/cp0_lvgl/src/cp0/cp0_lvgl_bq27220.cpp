#include "cp0_lvgl_app.h"
#include "hal_lvgl_bsp.h"
#include "../cp0_battery_api_contract.hpp"
#include "../cp0_callback_result.hpp"
#include "../cp0_battery_codec.hpp"
#include "../cp0_battery_lifecycle.hpp"
#include "../cp0_signal_registration.hpp"
#include "../cp0_battery_testable.hpp"
#include "../cp0_callback_contract.hpp"
#include "../cp0_battery_snapshot_cache.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <functional>
#include <condition_variable>
#include <chrono>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <unistd.h>

#if __has_include(<sys/ioctl.h>)
#include <sys/ioctl.h>
#endif

#if __has_include(<linux/i2c.h>) && __has_include(<linux/i2c-dev.h>)
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#define CP0_HAS_LINUX_I2C_RDWR 1
#else
#define CP0_HAS_LINUX_I2C_RDWR 0
#endif

namespace {

cp0::battery::Lifecycle &bq_lifecycle()
{
    static cp0::battery::Lifecycle lifecycle;
    return lifecycle;
}

class Bq27220System
{
public:
    using arg_t = std::list<std::string>;
    using callback_t = std::function<void(int, std::string)>;

    Bq27220System()
        : worker_([this] { sample_loop(); })
    {
    }

    ~Bq27220System()
    {
        stop();
    }

    void stop() noexcept
    {
        {
            std::lock_guard<std::mutex> lock(worker_mutex_);
            stopping_ = true;
        }
        worker_cv_.notify_all();
        if (worker_.joinable() && worker_.get_id() != std::this_thread::get_id())
            worker_.join();
    }

    void api_call(arg_t arg, callback_t callback)
    {
        cp0::CallbackResult result(std::move(callback));
        result.guard(-1, "battery api failure", [&] {
            cp0::battery::ApiReply reply;
            {
                std::lock_guard<std::mutex> lock(api_mutex_);
                reply = cp0::battery::dispatch_api_request(
                    arg, [this] { return read_cached(); },
                    [this](int index) { return calibrate(index); });
            }
            result.complete(reply.code, reply.data);
        });
    }

    cp0_battery_info_t read_hardware()
    {
        cp0_battery_info_t info{};

        char bq_path[256] = {0};
        long present = 0, capacity = 0, voltage_uv = 0, current_raw = 0, temp_raw = 0;
        char status[64] = "Unknown";

        if (find_power_supply(bq_path, sizeof(bq_path))) {
            char path[320];
            std::snprintf(path, sizeof(path), "%s/present", bq_path);
            int ok = read_long(path, &present);
            std::snprintf(path, sizeof(path), "%s/capacity", bq_path);
            ok = ok && read_long(path, &capacity);
            std::snprintf(path, sizeof(path), "%s/voltage_now", bq_path);
            ok = ok && read_long(path, &voltage_uv);
            ok = ok && read_current_raw(bq_path, &current_raw);
            std::snprintf(path, sizeof(path), "%s/temp", bq_path);
            ok = ok && read_long(path, &temp_raw);
            std::snprintf(path, sizeof(path), "%s/status", bq_path);
            ok = ok && read_string(path, status, sizeof(status));

            if (ok) {
                return cp0::battery::from_power_supply(
                    static_cast<int>(present), static_cast<int>(capacity),
                    voltage_uv, current_raw, static_cast<int>(temp_raw), status);
            }
        }

        return info;
    }

    cp0_battery_info_t read_cached()
    {
        return cache_.read();
    }

    int calibrate(int command_index)
    {
#if CP0_HAS_LINUX_I2C_RDWR
        static const int cmds[] = {0x0081, 0x000A, 0x0009, 0x0080};
        if (command_index < 0 || command_index >= static_cast<int>(sizeof(cmds) / sizeof(cmds[0]))) {
            return -1;
        }

        int fd = open(kI2cDev, O_RDWR);
        if (fd < 0) {
            return -errno;
        }

        struct i2c_msg msg;
        struct i2c_rdwr_ioctl_data data;
        unsigned char buf[3] = {0x00,
                                static_cast<unsigned char>(cmds[command_index] & 0xFF),
                                static_cast<unsigned char>((cmds[command_index] >> 8) & 0xFF)};
        msg.addr = kI2cAddr;
        msg.flags = 0;
        msg.len = 3;
        msg.buf = buf;
        data.msgs = &msg;
        data.nmsgs = 1;

        int ret = ioctl(fd, I2C_RDWR, &data);
        int saved_errno = errno;
        close(fd);
        return ret >= 0 ? 0 : -saved_errno;
#else
        (void)command_index;
        return -1;
#endif
    }

private:
    static constexpr std::chrono::seconds kSamplePeriod{3};

    void sample_loop() noexcept
    {
        while (true) {
            cp0_battery_info_t info{};
            try {
                std::lock_guard<std::mutex> lock(api_mutex_);
                info = read_hardware();
            } catch (...) {
                info = {};
            }
            if (info.valid == 1) {
                cache_.update(info);
            }

            std::unique_lock<std::mutex> lock(worker_mutex_);
            if (worker_cv_.wait_for(lock, kSamplePeriod, [this] { return stopping_; }))
                return;
        }
    }

    std::mutex api_mutex_;
    std::mutex worker_mutex_;
    std::condition_variable worker_cv_;
    std::thread worker_;
    bool stopping_ = false;
    cp0::battery::SnapshotCache cache_;

    static constexpr const char *kI2cDev = "/dev/i2c-1";
    static constexpr int kI2cAddr = 0x55;

    static int read_long(const char *path, long *value)
    {
        if (!path || !value) return 0;
        FILE *fp = std::fopen(path, "r");
        if (!fp) return 0;
        long v = 0;
        int ret = std::fscanf(fp, "%ld", &v);
        std::fclose(fp);
        if (ret != 1) return 0;
        *value = v;
        return 1;
    }

    static int read_current_raw(const char *dir, long *value)
    {
        if (!dir || !value) return 0;
        char path[320];
        std::snprintf(path, sizeof(path), "%s/current_instant", dir);
        if (read_long(path, value)) return 1;
        std::snprintf(path, sizeof(path), "%s/current_now", dir);
        return read_long(path, value);
    }

    static int read_string(const char *path, char *buf, size_t len)
    {
        if (!path || !buf || len == 0) return 0;
        FILE *fp = std::fopen(path, "r");
        if (!fp) return 0;
        if (!std::fgets(buf, static_cast<int>(len), fp)) {
            std::fclose(fp);
            return 0;
        }
        std::fclose(fp);
        size_t n = std::strlen(buf);
        while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) {
            buf[--n] = 0;
        }
        return 1;
    }

    static int has_file(const char *dir, const char *name)
    {
        if (!dir || !name) return 0;
        const std::string path = std::string(dir) + "/" + name;
        return access(path.c_str(), R_OK) == 0;
    }

    static int find_power_supply(char *out, size_t out_len)
    {
        const char *base = "/sys/class/power_supply";
        DIR *dp = opendir(base);
        if (!dp) return 0;

        std::string fallback;
        struct dirent *ent = nullptr;
        while ((ent = readdir(dp)) != nullptr) {
            if (ent->d_name[0] == '.') continue;

            const std::string dir = std::string(base) + "/" + ent->d_name;
            char type[64] = {0};
            const std::string type_path = dir + "/type";
            if (!read_string(type_path.c_str(), type, sizeof(type)) ||
                !cp0_battery_testable::power_supply_type_is_battery(type) ||
                !has_file(dir.c_str(), "present") ||
                !has_file(dir.c_str(), "capacity") ||
                !has_file(dir.c_str(), "voltage_now") ||
                (!has_file(dir.c_str(), "current_instant") && !has_file(dir.c_str(), "current_now")) ||
                !has_file(dir.c_str(), "temp") ||
                !has_file(dir.c_str(), "status")) {
                continue;
            }

            if (std::strstr(ent->d_name, "bq27220") || std::strstr(ent->d_name, "bq27")) {
                if (dir.size() >= out_len) {
                    closedir(dp);
                    return 0;
                }
                std::memcpy(out, dir.c_str(), dir.size() + 1);
                closedir(dp);
                return 1;
            }
            if (fallback.empty()) fallback = dir;
        }

        closedir(dp);
        if (!fallback.empty() && fallback.size() < out_len) {
            std::memcpy(out, fallback.c_str(), fallback.size() + 1);
            return 1;
        }
        return 0;
    }

};

} // namespace

extern "C" {

cp0_battery_info_t cp0_battery_read(void)
{
    cp0_battery_info_t info{};
    try {
        cp0_signal_bq27220_api({"Read"}, [&](int code, std::string data) {
            if (code == 0) cp0::battery::decode_info(data, &info);
        });
    } catch (...) {
        info = {};
    }
    return info;
}

int cp0_bq27220_calibrate(int command_index)
{
    int ret = -1;
    try {
        cp0_signal_bq27220_api({"Calibrate", std::to_string(command_index)}, [&](int code, std::string data) {
            (void)data;
            ret = code;
        });
    } catch (...) {
        ret = -1;
    }
    return ret;
}

void init_bq27220(void)
{
    bq_lifecycle().start(
        [] {
            auto bq27220 = std::make_shared<Bq27220System>();
            using Registration = cp0::SignalRegistration<decltype(cp0_signal_bq27220_api)>;
            auto registration = std::make_shared<Registration>();
            const bool registered = registration->replace(
                cp0_signal_bq27220_api,
                [bq27220](std::list<std::string> arg,
                          std::function<void(int, std::string)> callback) {
                    bq27220->api_call(std::move(arg), std::move(callback));
                });
            return cp0::battery::LifecycleResource{
                registered, [registration, bq27220] {
                    registration->reset();
                    bq27220->stop();
                }};
        },
        [] { return cp0::battery::LifecycleResource{true, {}}; });
}

void deinit_bq27220(void)
{
    bq_lifecycle().stop();
}

}
