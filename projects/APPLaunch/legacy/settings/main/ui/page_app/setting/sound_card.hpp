#pragma once

#include "menu_types.hpp"
#include "../../model/sound_card_model.hpp"

#include <lvgl.h>

#include <cstdint>
#include <string>
#include <vector>

class UISetupPage;

namespace setting {

class SoundCard
{
public:
    SoundCard() = default;
    ~SoundCard();
    void shutdown();

    void append(UISetupPage &page, std::vector<MenuItem> &menu);
    void enter_cards(UISetupPage &page);
    void enter_controls(UISetupPage &page);
    void enter_detail(UISetupPage &page);
    void build_cards_view(UISetupPage &page);
    void build_controls_view(UISetupPage &page);
    void build_detail_view(UISetupPage &page);
    void handle_cards_key(UISetupPage &page, uint32_t key);
    void handle_controls_key(UISetupPage &page, uint32_t key);
    void handle_detail_key(UISetupPage &page, uint32_t key);

private:
    void input_update_display();
    void cursor_stop();
    void clear_detail_view();
    void stop_transition_timer();
    static void cursor_timer_cb(lv_timer_t *timer) noexcept;
    static void input_label_delete_cb(lv_event_t *event) noexcept;
    static void hint_label_delete_cb(lv_event_t *event) noexcept;
    static void transition_timer_cb(lv_timer_t *timer) noexcept;
    static void transition_screen_delete_cb(lv_event_t *event) noexcept;
    void apply_value(UISetupPage &page);

    std::vector<SoundCardInfo> cards_;
    std::vector<SoundControlInfo> controls_;
    int card_sel_ = 0;
    int ctrl_sel_ = 0;
    int card_idx_ = -1;
    SoundControlInfo detail_;
    std::string input_buf_;
    lv_obj_t *input_lbl_ = nullptr;
    lv_obj_t *hint_lbl_ = nullptr;
    lv_timer_t *cursor_timer_ = nullptr;
    bool cursor_callback_enabled_ = false;
    lv_timer_t *transition_timer_ = nullptr;
    UISetupPage *transition_page_ = nullptr;
    SoundCardTransitionModel transition_model_;
    bool cursor_vis_ = true;
};

} // namespace setting
