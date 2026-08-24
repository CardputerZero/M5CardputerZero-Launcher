#include "settings_system_api.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cerrno>
#include <mutex>
#include <memory>
#include <utility>

namespace settings_system {
namespace {

struct SynchronousResult {
    std::mutex mutex;
    std::condition_variable condition;
    bool completed = false;
    int code = -1;
    std::string payload;
};

int call_sync(std::list<std::string> arguments, std::string &payload)
{
    auto result = std::make_shared<SynchronousResult>();
    try {
        cp0_signal_osinfo_api(
            std::move(arguments),
            [result](int code, std::string data) {
                std::lock_guard<std::mutex> lock(result->mutex);
                if (result->completed) return;
                result->completed = true;
                result->code = code;
                result->payload = std::move(data);
                result->condition.notify_one();
            });
    } catch (...) {
        return -1;
    }

    std::unique_lock<std::mutex> lock(result->mutex);
    if (!result->condition.wait_for(lock, std::chrono::seconds(15), [result] {
            return result->completed;
        }))
        return -ETIMEDOUT;
    payload = std::move(result->payload);
    return result->code;
}

int copy_network(const cp0_eth_info_t &raw, NetworkInfo &result)
{
    result = {raw.ipv4, raw.gateway, raw.mac};
    return 0;
}

} // namespace

void request(std::list<std::string> arguments, ApiCallback callback) noexcept
{
    if (!callback) return;

    auto delivered = std::make_shared<std::atomic_bool>(false);
    auto safe_callback = [callback = std::move(callback), delivered](int code,
                                                                       std::string data) mutable {
        bool expected = false;
        if (!delivered->compare_exchange_strong(expected, true)) return;
        try {
            callback(code, std::move(data));
        } catch (...) {
        }
    };

    try {
        cp0_signal_osinfo_api(std::move(arguments), safe_callback);
    } catch (...) {
        safe_callback(-1, "osinfo service unavailable");
    }
}

void request_background(UpdateAction action, ApiCallback callback) noexcept
{
    request({background_update_request(action)}, std::move(callback));
}

int read_network_default(NetworkInfo &result)
{
    cp0_eth_info_t raw{};
    const int code = cp0_network_default_info_read(&raw);
    if (code != 0) return code;
    return copy_network(raw, result);
}

int read_ethernet(NetworkInfo &result)
{
    cp0_eth_info_t raw{};
    const int code = cp0_eth_info_read(&raw);
    if (code != 0) return code;
    return copy_network(raw, result);
}

int read_account(AccountInfo &result)
{
    cp0_account_info_t raw{};
    const int code = cp0_account_info_read(&raw);
    if (code != 0) return code;
    result = {raw.user, raw.hostname};
    return 0;
}

int read_launcher_state(std::string &state)
{
    state.clear();
    return call_sync({"UpdateLauncherState"}, state);
}

int start_update(UpdateAction action, std::string &job_id)
{
    job_id.clear();
    std::string payload;
    const int code = call_sync({update_request(action)}, payload);
    if (code == 0 && payload.empty()) return -1;
    if (code == 0) job_id = std::move(payload);
    return code;
}

int update_status(const std::string &job_id, std::string &state)
{
    state.clear();
    if (job_id.empty()) return -1;
    return call_sync({"UpdateJobStatus", job_id}, state);
}

int cancel_update(const std::string &job_id)
{
    if (job_id.empty()) return -1;
    std::string ignored;
    return call_sync({"UpdateJobCancel", job_id}, ignored);
}

int apt_update_background()
{
    return cp0_system_apt_update_background();
}

int update_launcher_background()
{
    return cp0_system_update_launcher_background();
}

} // namespace settings_system
