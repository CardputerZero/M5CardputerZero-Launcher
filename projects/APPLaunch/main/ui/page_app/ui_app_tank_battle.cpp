#define APP_PAGE_IMPLEMENTATION_UNIT
#include "ui_app_tank_battle.hpp"

#include "input_keys.h"

#include <keyboard_input.h>

UITankBattlePage::UITankBattlePage() : AppPageRoot()
{
    init_game_state();
    creat_UI();
    event_handler_init();
    if (tank_battle_ui_ready(background_, arena_, player_obj_)) {
        tick_callback_enabled_ = tick_timer_.start(
            [this] { return lv_timer_create(UITankBattlePage::static_tick_cb, 80, this); },
            [](lv_timer_t *timer) { lv_timer_delete(timer); });
    }
}

UITankBattlePage::~UITankBattlePage()
{
    tick_timer_.stop();
    if (root_screen_)
        lv_obj_remove_event_cb_with_user_data(
            root_screen_, UITankBattlePage::static_lvgl_handler, this);
    detach_ui_callbacks();
}

void UITankBattlePage::init_game_state()
{
    model_.reset();
}

void UITankBattlePage::event_handler_init()
{
    if (!root_screen_) return;
    lv_obj_add_event_cb(root_screen_, UITankBattlePage::static_lvgl_handler, LV_EVENT_ALL, this);
}

void UITankBattlePage::detach_ui_callbacks()
{
    std::vector<lv_obj_t *> objects = {background_, arena_, status_label_, player_obj_,
                                      game_msg_panel_, game_msg_label_, help_panel_, help_label_};
    for (const TankVisual &visual : enemy_tanks_) objects.push_back(visual.root);
    objects.insert(objects.end(), bullet_objs_.begin(), bullet_objs_.end());
    for (lv_obj_t *object : objects)
        if (object)
            lv_obj_remove_event_cb_with_user_data(object, owned_obj_delete_cb, this);
}

void UITankBattlePage::owned_obj_delete_cb(lv_event_t *event) noexcept
{
    try {
        if (!event || !tank_battle_owned_delete_callback_allowed(
                lv_event_get_target(event), lv_event_get_current_target(event))) return;
        auto *self = static_cast<UITankBattlePage *>(lv_event_get_user_data(event));
        auto *deleted = static_cast<lv_obj_t *>(lv_event_get_target(event));
        if (!self) return;

        if (self->game_msg_label_ == deleted) self->game_msg_label_ = nullptr;
        if (self->game_msg_panel_ == deleted) {
            self->game_msg_panel_ = nullptr;
            self->game_msg_label_ = nullptr;
        }
        if (self->help_label_ == deleted) self->help_label_ = nullptr;
        if (self->help_panel_ == deleted) {
            self->help_panel_ = nullptr;
            self->help_label_ = nullptr;
        }
        if (self->player_obj_ == deleted) {
            self->player_obj_ = nullptr;
            self->player_tank_ = {};
            self->tick_timer_.stop();
        }
        if (self->status_label_ == deleted) {
            self->status_label_ = nullptr;
            self->ui_obj_.erase("status");
        }
        for (TankVisual &visual : self->enemy_tanks_)
            if (visual.root == deleted) visual = {};
        for (lv_obj_t *&object : self->bullet_objs_)
            if (object == deleted) object = nullptr;
        if (self->arena_ == deleted) {
            self->tick_timer_.stop();
            self->arena_ = nullptr;
            self->player_obj_ = nullptr;
            self->player_tank_ = {};
            self->enemy_tanks_.clear();
            self->bullet_objs_.clear();
            self->game_msg_panel_ = nullptr;
            self->game_msg_label_ = nullptr;
            self->help_panel_ = nullptr;
            self->help_label_ = nullptr;
            self->ui_obj_.erase("arena");
        }
        if (self->background_ == deleted) {
            self->tick_timer_.stop();
            self->background_ = nullptr;
            self->arena_ = nullptr;
            self->status_label_ = nullptr;
            self->player_obj_ = nullptr;
            self->player_tank_ = {};
            self->enemy_tanks_.clear();
            self->bullet_objs_.clear();
            self->game_msg_panel_ = nullptr;
            self->game_msg_label_ = nullptr;
            self->help_panel_ = nullptr;
            self->help_label_ = nullptr;
            self->ui_obj_.clear();
        }
    } catch (...) {
        // LVGL callbacks must not propagate C++ exceptions across the C boundary.
    }
}

void UITankBattlePage::static_lvgl_handler(lv_event_t *e) noexcept
{
    UITankBattlePage *self = nullptr;
    try {
        if (!e) return;
        self = static_cast<UITankBattlePage *>(lv_event_get_user_data(e));
        if (!self || !tank_battle_root_event_callback_allowed(
                lv_event_get_current_target(e), self->root_screen_)) return;
        self->event_handler(e);
    } catch (...) {
        if (self) self->tick_callback_enabled_ = false;
    }
}

void UITankBattlePage::event_handler(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_DELETE &&
        lv_event_get_target(e) == lv_event_get_current_target(e)) {
        tick_timer_.stop();
        player_obj_ = nullptr;
        player_tank_ = {};
        enemy_tanks_.clear();
        bullet_objs_.clear();
        game_msg_panel_ = nullptr;
        game_msg_label_ = nullptr;
        help_panel_ = nullptr;
        help_label_ = nullptr;
        return;
    }
    if (!launcher_ui::events::is_key_pressed(e) && !launcher_ui::events::is_key_released(e)) return;

    const key_item *item = launcher_ui::events::keyboard_item(e);
    const bool help_key = item &&
                          (item->key_code == KEY_HELP || item->semantic_key == KEY_HELP);
    if (help_key && launcher_ui::events::is_key_pressed(e)) {
        toggle_help_view();
        return;
    }

    const GameInputAction action = game_input_action(item);
    if (help_panel_ && !lv_obj_has_flag(help_panel_, LV_OBJ_FLAG_HIDDEN)) {
        if (action == GameInputAction::CANCEL && launcher_ui::events::is_key_released(e))
            toggle_help_view();
        return;
    }

    if (action == GameInputAction::CANCEL) {
        if (!launcher_ui::events::is_key_released(e)) return;
        tick_timer_.stop();
        if (navigate_home) navigate_home();
        return;
    }
    if (!launcher_ui::events::is_key_pressed(e)) return;

    if (model_.game_over()) {
        if (action == GameInputAction::FIRE ||
            action == GameInputAction::CONFIRM) {
            init_game_state();
            sync_scene();
        }
        return;
    }

    switch (action) {
    case GameInputAction::UP:
        model_.move_player(TankDirection::UP);
        break;
    case GameInputAction::DOWN:
        model_.move_player(TankDirection::DOWN);
        break;
    case GameInputAction::LEFT:
        model_.move_player(TankDirection::LEFT);
        break;
    case GameInputAction::RIGHT:
        model_.move_player(TankDirection::RIGHT);
        break;
    case GameInputAction::FIRE:
        model_.player_fire();
        break;
    default:
        break;
    }
    sync_scene();
}

void UITankBattlePage::static_tick_cb(lv_timer_t *timer) noexcept
{
    UITankBattlePage *self = nullptr;
    try {
        self = static_cast<UITankBattlePage *>(lv_timer_get_user_data(timer));
        if (!self || !self->tick_callback_enabled_) return;
        if (!tank_battle_tick_callback_allowed(
                self->tick_callback_enabled_, self->tick_timer_.current(timer),
                self->background_, self->arena_, self->player_obj_)) return;
        self->tick();
    } catch (...) {
        if (self) self->tick_callback_enabled_ = false;
    }
}

void UITankBattlePage::tick()
{
    if (model_.game_over()) return;
    model_.tick();
    sync_scene();
}

void UITankBattlePage::place_grid_obj(lv_obj_t *obj, int gx, int gy, int w, int h)
{
    const int px = GRID_OX + gx * CELL + (CELL - w) / 2;
    const int py = GRID_OY + gy * CELL + (CELL - h) / 2;
    lv_obj_set_pos(obj, px, py);
}

void UITankBattlePage::sync_scene()
{
    const auto &player = model_.player();
    const auto &enemies = model_.enemies();
    const auto &bullets = model_.bullets();
    if (player_obj_) {
        place_grid_obj(player_obj_, player.x, player.y, CELL - 2, CELL - 2);
        sync_tank_visual(player_tank_, player.direction, 0x35D07F);
        player.alive ? lv_obj_clear_flag(player_obj_, LV_OBJ_FLAG_HIDDEN)
                     : lv_obj_add_flag(player_obj_, LV_OBJ_FLAG_HIDDEN);
    }

    for (size_t i = 0; i < enemy_tanks_.size() && i < enemies.size(); ++i) {
        TankVisual &visual = enemy_tanks_[i];
        lv_obj_t *obj = visual.root;
        if (!obj) continue;
        if (enemies[i].alive) {
            place_grid_obj(obj, enemies[i].x, enemies[i].y, CELL - 2, CELL - 2);
            sync_tank_visual(visual, enemies[i].direction, 0xE65045);
            lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
        }
    }

    for (size_t i = 0; i < bullet_objs_.size() && i < bullets.size(); ++i) {
        lv_obj_t *obj = bullet_objs_[i];
        if (!obj) continue;
        const TankBattleBullet &bullet = bullets[i];
        if (bullet.alive) {
            place_grid_obj(obj, bullet.x, bullet.y, 4, 4);
            lv_obj_set_style_bg_color(obj,
                                      bullet.from_player ? lv_color_hex(0xF4D03F)
                                                         : lv_color_hex(0xFF8C42),
                                      LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
        }
    }

    char status_buf[96];
    lv_snprintf(status_buf, sizeof(status_buf), "Score:%d  Enemy:%d", model_.score(),
                model_.alive_enemy_count());
    if (status_label_) lv_label_set_text(status_label_, status_buf);
    sync_game_message();
}

void UITankBattlePage::sync_game_message()
{
    if (!game_msg_panel_ || !game_msg_label_) return;
    if (!model_.game_over()) {
        lv_obj_add_flag(game_msg_panel_, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_label_set_text(game_msg_label_, model_.won() ? "YOU WIN\nPress Space restart"
                                                    : "GAME OVER\nPress Space restart");
    lv_obj_set_style_border_color(game_msg_panel_,
                                  lv_color_hex(model_.won() ? 0x2ECC71 : 0xE74C3C),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(game_msg_label_);
    lv_obj_clear_flag(game_msg_panel_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(game_msg_panel_);
}
