#pragma once

#include <cstdint>
#include <chrono>
#include <functional>
#include <list>
#include <string>

#include "lvgl_components.hpp"

class LvSettingBrightnessPage3 : public LvSettingValuePage3Base {
public:
    using Arguments = std::list<std::string>;
    using Callback = std::function<void(int, std::string)>;
    using SettingsInvoker = std::function<void(Arguments, Callback)>;
    using ConfigInvoker = std::function<void(Arguments, Callback)>;

    enum class BrightnessReadStatus : uint8_t { Ok, Defaulted, BackendError, InvalidPayload };
    struct BrightnessReadResult {
        BrightnessReadStatus status = BrightnessReadStatus::BackendError;
        int maximum = 100;
        int value = 75;
        int index = 0;
        std::string message;
        bool usable() const noexcept;
        bool defaulted() const noexcept;
    };
    enum class BrightnessWriteStatus : uint8_t {
        Ok, InvalidTarget, ConfigReadFailed, BacklightWriteFailed,
        BacklightPayloadInvalid, ConfigWriteFailed, SaveFailed, RollbackFailed
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

    LvSettingBrightnessPage3();
    LvSettingBrightnessPage3(lv_obj_t *parent, const NodeIter &parent_node);
    LvSettingBrightnessPage3(lv_obj_t *parent,
                             const NodeIter &parent_node,
                             std::function<void()> back_callback);
    ~LvSettingBrightnessPage3() override;

protected:
    int initial_selection() const override;
    SettingApiResult activate_selected() override;

private:
    static SettingsInvoker settings_invoker();
    static ConfigInvoker config_invoker();

    int option_count() const;
    void initialize_page(lv_obj_t *parent, std::function<void()> back_callback);
    void request_back();
    void restore_focus();
    bool request_is_current(std::uint64_t generation) const;
    void create_status_label();
    void set_status(const std::string &text, bool error);
    void finish_load(const BrightnessReadResult &result);
    void finish_load_failure();
    void begin_load();
    void finish_write(const BrightnessWriteResult &result);
    void finish_write_failure();
    bool begin_write();

    bool destroying_ = false;
    bool back_requested_ = false;
    bool page_alive_ = true;
    bool pending_ = false;
    bool loaded_ = false;
    std::uint64_t generation_ = 0;
    int maximum_ = 100;
    int value_ = 75;
    int saved_index_ = 0;
    lv_obj_t *status_label_ = nullptr;
    std::function<void()> back_callback_;
};
