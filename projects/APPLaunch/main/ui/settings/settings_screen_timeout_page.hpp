#pragma once

#include "hal_lvgl_bsp.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <list>
#include <string>

#include "lvgl_components.hpp"

class LvSettingDarkTimePage3 : public LvSettingValuePage3Base {
public:
    using Arguments = std::list<std::string>;
    using Callback = std::function<void(int, std::string)>;
    using ConfigInvoker = std::function<void(Arguments, Callback)>;
    enum class DefaultValue : int {
        DarkTimeSeconds = 30,
    };

    enum class DarkTimeReadStatus : uint8_t { Ok, Defaulted, BackendError, InvalidPayload };
    struct DarkTimeReadResult {
        DarkTimeReadStatus status = DarkTimeReadStatus::BackendError;
        int seconds = static_cast<int>(DefaultValue::DarkTimeSeconds);
        int index = 2;
        std::string message;
        bool usable() const noexcept;
        bool defaulted() const noexcept;
    };
    enum class DarkTimeWriteStatus : uint8_t {
        Ok, InvalidTarget, ReadFailed, SetFailed, SaveFailed, RollbackFailed
    };
    struct DarkTimeWriteResult {
        DarkTimeWriteStatus status = DarkTimeWriteStatus::ReadFailed;
        int previous_seconds = static_cast<int>(DefaultValue::DarkTimeSeconds);
        int previous_index = 2;
        int applied_seconds = static_cast<int>(DefaultValue::DarkTimeSeconds);
        bool rollback_attempted = false;
        bool rollback_succeeded = false;
        std::string message;
        bool succeeded() const noexcept;
    };

    enum class LayoutMetric : int {
        StatusLabelW = 104,
        StatusLabelX = 4,
        StatusLabelY = 4,
    };

    static constexpr int metric(LayoutMetric value)
    {
        return static_cast<int>(value);
    }

    LvSettingDarkTimePage3();

    LvSettingDarkTimePage3(lv_obj_t *parent, const NodeIter &parent_node);

    LvSettingDarkTimePage3(lv_obj_t *parent,
                           const NodeIter &parent_node,
                           std::function<void()> back_callback);

    ~LvSettingDarkTimePage3() override;

protected:
    int initial_selection() const override;

    SettingApiResult activate_selected() override;

private:
    static ConfigInvoker config_invoker();

    void initialize_page(lv_obj_t *parent, std::function<void()> back_callback);

    void request_back();

    void restore_focus();

    bool request_is_current(std::uint64_t generation) const;

    void create_status_label();

    void set_status(const std::string &text, bool error);

    void finish_load(const DarkTimeReadResult &result);

    void finish_load_failure();

    void begin_load();

    void finish_write(const DarkTimeWriteResult &result);

    void finish_write_failure();

    bool begin_write();

    bool destroying_ = false;
    bool back_requested_ = false;
    bool page_alive_ = true;
    bool pending_ = false;
    bool loaded_ = false;
    std::uint64_t generation_ = 0;
    int saved_seconds_ = static_cast<int>(DefaultValue::DarkTimeSeconds);
    int saved_index_ = 2;
    lv_obj_t *status_label_ = nullptr;
    std::function<void()> back_callback_;
};
