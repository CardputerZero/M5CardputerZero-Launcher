#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "cp0_font_service.hpp"
#include "cp0_lvgl_app_page_assets.h"
#include "hal_lvgl_bsp.h"
#include "input_keys.h"
#include "keyboard_input.h"
#include "settings_sound_card_adapter.hpp"
#include "settings_tree_types.hpp"

#define LVGL_COMPONENTS_ROLLER1_ONLY
#include "lvgl_components.hpp"
#undef LVGL_COMPONENTS_ROLLER1_ONLY

class LvSettingSoundCardDetailPage : public DComponens::LvglComponensBase {
public:
    using Control = ui_test_soundcard::Control;
    using SubmitCallback = std::function<void(std::string)>;

    enum class LayoutMetric : int {
        ScreenW = 320,
        ScreenH = 150,
        TitleW = 180,
        ContentW = 304,
        TitleY = 6,
        CardY = 26,
        LimitsY = 44,
        CurrentY = 62,
        InputY = 84,
        StatusY = 112,
    };

    static constexpr int metric(LayoutMetric value)
    {
        return static_cast<int>(value);
    }

    LvSettingSoundCardDetailPage();

    LvSettingSoundCardDetailPage(lv_obj_t *parent,
                                 int card_index,
                                 Control control,
                                 std::function<void()> back_callback,
                                 SubmitCallback submit_callback = {});

    void AnimateNextIn(std::function<void()> animate_over_func) override;
    void AnimateNextOut(std::function<void()> animate_over_func) override;
    void LoadNextPage() override;
    void LeaveNextPage() override;

    const Control &control() const;
    bool detail_loaded() const;
    bool writing() const;

    void set_detail(Control detail);
    void set_detail_error(const std::string &message);
    void begin_write(const std::string &wire_value);
    void mark_refresh_pending();
    void complete_write_success(Control actual);
    void complete_write_failure(const std::string &message);

    ~LvSettingSoundCardDetailPage() override;

    void create_ui(lv_obj_t *parent) override;

private:
    static lv_obj_t *create_label(lv_obj_t *parent,
                                  const char *text,
                                  int x,
                                  int y,
                                  int width,
                                  int height,
                                  uint32_t color,
                                  int font_size);

    std::string card_label() const;
    std::string limits_text() const;
    std::string current_text() const;
    void update_labels();
    void set_status(const std::string &text, uint32_t color);
    void select_current_option();
    static int digit_from_key(uint32_t key);
    void append_digit(int digit);
    void select_enum_option(int direction);
    void submit_value();
    void handle_key(uint32_t key);
    void handle_key_event(lv_event_t *event);
    static void keyboard_event_cb(lv_event_t *event);

    int card_index_ = -1;
    Control control_;
    SubmitCallback submit_callback_;
    std::string input_text_;
    std::string pending_wire_value_;
    int selected_option_ = 0;
    bool loading_ = true;
    bool detail_loaded_ = false;
    bool writing_ = false;
    lv_obj_t *event_root_ = nullptr;
    lv_event_dsc_t *keyboard_event_dsc_ = nullptr;
    lv_obj_t *title_ = nullptr;
    lv_obj_t *card_ = nullptr;
    lv_obj_t *limits_ = nullptr;
    lv_obj_t *current_ = nullptr;
    lv_obj_t *input_ = nullptr;
    lv_obj_t *status_ = nullptr;
};

SettingPageFactory soundcard_detail_page_factory(
    int card_index,
    ui_test_soundcard::Control control,
    LvSettingSoundCardDetailPage::SubmitCallback submit_callback = {});
