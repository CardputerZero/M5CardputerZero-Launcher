/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "../launcher_ui_app_page.hpp"
#include "../model/tank_battle_model.hpp"
#include "../model/tank_battle_page_contract.hpp"
#include "../model/page_timer_lifecycle.hpp"
#include "../model/game_input_action.hpp"
#include "cp0_keyboard_input_context.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

class UITankBattlePage : public AppPageRoot
{
private:
    struct TankVisual {
        lv_obj_t *root = nullptr;
        lv_obj_t *left_track = nullptr;
        lv_obj_t *right_track = nullptr;
        lv_obj_t *hull = nullptr;
        lv_obj_t *turret = nullptr;
        lv_obj_t *barrel = nullptr;
    };

    static constexpr int SCREEN_W = 320;
    static constexpr int SCREEN_H = 170;

    static constexpr int GRID_COLS = TankBattleModel::GRID_COLUMNS;
    static constexpr int GRID_ROWS = TankBattleModel::GRID_ROWS;
    static constexpr int CELL      = 14;

    static constexpr int ARENA_X = 8;
    static constexpr int ARENA_Y = 42;
    static constexpr int ARENA_W = 304;
    static constexpr int ARENA_H = 124;

    static constexpr int GRID_W  = GRID_COLS * CELL;
    static constexpr int GRID_H  = GRID_ROWS * CELL;
    static constexpr int GRID_OX = (ARENA_W - GRID_W) / 2;
    static constexpr int GRID_OY = (ARENA_H - GRID_H) / 2;

public:
    UITankBattlePage();
    ~UITankBattlePage();

private:
    std::unordered_map<std::string, lv_obj_t *> ui_obj_;

    PageTimerLifecycle<lv_timer_t *> tick_timer_;
    bool tick_callback_enabled_ = false;
    TankBattleModel model_;
    Cp0KeyboardInputContextScope input_context_scope_{KBD_INPUT_CONTEXT_GAME};

    TankVisual player_tank_;
    lv_obj_t *player_obj_ = nullptr;
    lv_obj_t *background_ = nullptr;
    lv_obj_t *arena_ = nullptr;
    lv_obj_t *status_label_ = nullptr;
    std::vector<TankVisual> enemy_tanks_;
    std::vector<lv_obj_t *> bullet_objs_;

    lv_obj_t *game_msg_panel_ = nullptr;
    lv_obj_t *game_msg_label_ = nullptr;
    lv_obj_t *help_panel_ = nullptr;
    lv_obj_t *help_label_ = nullptr;

private:
    void init_game_state();

private:
    void creat_UI();
    void create_game_message_panel(lv_obj_t *arena);
    bool build_help_view();
    void toggle_help_view();
    TankVisual create_tank_visual(lv_obj_t *arena, std::uint32_t armor_color);
    void sync_tank_visual(TankVisual &visual, TankDirection direction,
                          std::uint32_t armor_color);

private:
    void event_handler_init();
    void detach_ui_callbacks();
    static void owned_obj_delete_cb(lv_event_t *event) noexcept;
    static void static_lvgl_handler(lv_event_t *e) noexcept;
    void event_handler(lv_event_t *e);

private:
    static void static_tick_cb(lv_timer_t *t) noexcept;
    void tick();

private:
    void place_grid_obj(lv_obj_t *obj, int gx, int gy, int w, int h);
    void sync_scene();
    void sync_game_message();
};
