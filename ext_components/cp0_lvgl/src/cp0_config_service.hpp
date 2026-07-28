#pragma once

#include <cstddef>
#include <functional>
#include <list>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace cp0cfg {

using Entries = std::vector<std::pair<std::string, std::string>>;

class ConfigService {
public:
    using Callback = std::function<void(int, std::string)>;
    using Arguments = std::list<std::string>;
    using Loader = std::function<bool(Entries &)>;
    using Saver = std::function<int(const Entries &)>;

    ConfigService(Loader loader, Saver saver, std::string error_suffix = {});

    void init();
    void api_call(Arguments arguments, Callback callback);

private:
    static constexpr size_t kMaxEntries = 64;
    static constexpr size_t kMaxKeyLength = 63;
    static constexpr size_t kMaxValueLength = 255;

    Loader loader_;
    Saver saver_;
    std::string error_suffix_;
    Entries entries_;
    mutable std::mutex mutex_;
    std::mutex mutation_mutex_;

    std::string get_string(const std::string &key, const std::string &fallback) const;
    int get_integer(const std::string &key, int fallback) const;
    void set_string(const std::string &key, const std::string &value);
    void set_integer(const std::string &key, int value);
    bool reload();
    int save(const Entries &entries) const;
};

int parse_legacy_integer(const std::string &value, int fallback_if_empty);

} // namespace cp0cfg
