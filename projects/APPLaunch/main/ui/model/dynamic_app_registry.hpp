#pragma once

#include "../app_registry.h"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

struct DynamicAppRegistration {
    AppDescriptor desc{};
    std::string label;
    std::string icon;
    std::string config_key;

    void bind_descriptor()
    {
        desc = {label.c_str(), icon.c_str(), config_key.c_str(), true, false};
    }

    DynamicAppRegistration(std::string app_label,
                           std::string app_icon,
                           std::string app_config_key)
        : label(std::move(app_label)),
          icon(std::move(app_icon)),
          config_key(std::move(app_config_key))
    {
        bind_descriptor();
    }

    DynamicAppRegistration(const DynamicAppRegistration &other)
        : label(other.label), icon(other.icon), config_key(other.config_key)
    {
        bind_descriptor();
    }

    DynamicAppRegistration(DynamicAppRegistration &&other) noexcept
        : label(std::move(other.label)),
          icon(std::move(other.icon)),
          config_key(std::move(other.config_key))
    {
        bind_descriptor();
    }

    DynamicAppRegistration &operator=(const DynamicAppRegistration &other)
    {
        if (this == &other) return *this;
        label = other.label;
        icon = other.icon;
        config_key = other.config_key;
        bind_descriptor();
        return *this;
    }

    DynamicAppRegistration &operator=(DynamicAppRegistration &&other) noexcept
    {
        if (this == &other) return *this;
        label = std::move(other.label);
        icon = std::move(other.icon);
        config_key = std::move(other.config_key);
        bind_descriptor();
        return *this;
    }
};

class DynamicAppRegistry {
public:
    const std::vector<DynamicAppRegistration> &entries() const
    {
        return active_;
    }

    void begin_refresh()
    {
        pending_.clear();
        refreshing_ = true;
    }

    const AppDescriptor *register_pending(const std::string &label,
                                          const std::string &icon,
                                          const std::string &config_key)
    {
        if (!refreshing_ || label.empty() || config_key.empty()) return nullptr;
        pending_.emplace_back(label, icon, config_key);
        return &pending_.back().desc;
    }

    void commit_refresh()
    {
        if (!refreshing_) return;
        active_.swap(pending_);
        pending_.clear();
        refreshing_ = false;
    }

    void cancel_refresh()
    {
        if (!refreshing_) return;
        pending_.clear();
        refreshing_ = false;
    }

private:
    std::vector<DynamicAppRegistration> active_;
    std::vector<DynamicAppRegistration> pending_;
    bool refreshing_ = false;
};
