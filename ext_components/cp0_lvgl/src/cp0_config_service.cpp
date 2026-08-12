#include "cp0_config_service.hpp"
#include "cp0_integer_codec.hpp"

#include <utility>

namespace cp0cfg {
namespace {

std::string argument_at(const ConfigService::Arguments &arguments, size_t index)
{
    auto it = arguments.begin();
    for (size_t i = 0; i < index && it != arguments.end(); ++i)
        ++it;
    return it == arguments.end() ? std::string() : *it;
}

void report(const ConfigService::Callback &callback, int code, const std::string &data)
{
    if (!callback) return;
    try {
        callback(code, data);
    } catch (...) {
    }
}

} // namespace

int parse_legacy_integer(const std::string &value, int fallback_if_empty)
{
    return cp0_integer_codec::parse_int_prefix_clamped(value, fallback_if_empty);
}

ConfigService::ConfigService(Loader loader, Saver saver, std::string error_suffix)
    : loader_(std::move(loader)), saver_(std::move(saver)), error_suffix_(std::move(error_suffix))
{
    init();
}

void ConfigService::init()
{
    (void)reload();
}

bool ConfigService::reload()
{
    Entries loaded;
    try {
        if (!loader_ || !loader_(loaded)) return false;
    } catch (...) {
        return false;
    }

    Entries sanitized;
    sanitized.reserve(loaded.size() < kMaxEntries ? loaded.size() : kMaxEntries);
    for (const auto &entry : loaded) {
        if (entry.first.empty() || sanitized.size() >= kMaxEntries)
            continue;
        sanitized.emplace_back(entry.first.substr(0, kMaxKeyLength),
                               entry.second.substr(0, kMaxValueLength));
    }
    std::lock_guard<std::mutex> lock(mutex_);
    entries_ = std::move(sanitized);
    return true;
}

std::string ConfigService::get_string(const std::string &key, const std::string &fallback) const
{
    if (key.empty())
        return fallback;
    for (const auto &entry : entries_)
        if (entry.first == key)
            return entry.second;
    return fallback;
}

int ConfigService::get_integer(const std::string &key, int fallback) const
{
    if (key.empty())
        return fallback;
    for (const auto &entry : entries_)
        if (entry.first == key)
            return parse_legacy_integer(entry.second, 0);
    return fallback;
}

void ConfigService::set_string(const std::string &key, const std::string &value)
{
    if (key.empty())
        return;
    for (auto &entry : entries_) {
        if (entry.first == key) {
            entry.second = value.substr(0, kMaxValueLength);
            return;
        }
    }
    if (entries_.size() >= kMaxEntries)
        return;
    entries_.emplace_back(key.substr(0, kMaxKeyLength), value.substr(0, kMaxValueLength));
}

void ConfigService::set_integer(const std::string &key, int value)
{
    set_string(key, std::to_string(value));
}

int ConfigService::save(const Entries &entries) const
{
    try {
        return saver_ ? saver_(entries) : -1;
    } catch (...) {
        return -1;
    }
}

void ConfigService::api_call(Arguments arguments, Callback callback)
{
    if (arguments.empty()) {
        report(callback, -1, "empty config api" + error_suffix_);
        return;
    }

    const std::string command = arguments.front();
    if (command == "Init") {
        const bool loaded = reload();
        report(callback, loaded ? 0 : -1, loaded ? "ok" : "load failed");
        return;
    }
    if (command == "Save") {
        std::lock_guard<std::mutex> mutation_lock(mutation_mutex_);
        Entries snapshot;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            snapshot = entries_;
        }
        const int result = save(snapshot);
        report(callback, result, result == 0 ? "ok" : "save failed");
        return;
    }
    if (command == "SetManyAndSave") {
        std::lock_guard<std::mutex> mutation_lock(mutation_mutex_);
        if (arguments.size() < 3 || arguments.size() % 2 == 0) {
            report(callback, -1, "invalid config transaction" + error_suffix_);
            return;
        }
        Entries candidate;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            candidate = entries_;
        }
        auto set_candidate = [&](const std::string &key, const std::string &value) {
            if (key.empty()) return false;
            const std::string bounded_key = key.substr(0, kMaxKeyLength);
            const std::string bounded_value = value.substr(0, kMaxValueLength);
            for (auto &entry : candidate) {
                if (entry.first == bounded_key) {
                    entry.second = bounded_value;
                    return true;
                }
            }
            if (candidate.size() >= kMaxEntries) return false;
            candidate.emplace_back(bounded_key, bounded_value);
            return true;
        };
        auto it = std::next(arguments.begin());
        while (it != arguments.end()) {
            const std::string key = *it++;
            const std::string value = *it++;
            if (!set_candidate(key, value)) {
                report(callback, -1, "invalid config transaction" + error_suffix_);
                return;
            }
        }
        const int result = save(candidate);
        if (result == 0) {
            std::lock_guard<std::mutex> lock(mutex_);
            entries_ = std::move(candidate);
        }
        report(callback, result, result == 0 ? "ok" : "save failed");
        return;
    }

    int result_code = 0;
    std::string result_data;
    {
        std::unique_lock<std::mutex> mutation_lock;
        if (command == "SetInt" || command == "SetStr")
            mutation_lock = std::unique_lock<std::mutex>(mutation_mutex_);
        std::lock_guard<std::mutex> lock(mutex_);
        if (command == "GetInt") {
            const int fallback = parse_legacy_integer(argument_at(arguments, 2), 0);
            result_data = std::to_string(get_integer(argument_at(arguments, 1), fallback));
        } else if (command == "SetInt") {
            set_integer(argument_at(arguments, 1), parse_legacy_integer(argument_at(arguments, 2), 0));
            result_data = "ok";
        } else if (command == "GetStr") {
            result_data = get_string(argument_at(arguments, 1), argument_at(arguments, 2));
        } else if (command == "SetStr") {
            set_string(argument_at(arguments, 1), argument_at(arguments, 2));
            result_data = "ok";
        } else {
            result_code = -1;
            result_data = "unknown config api" + error_suffix_;
        }
    }
    report(callback, result_code, result_data);
}

} // namespace cp0cfg
