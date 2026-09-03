/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once
#include "../launcher_ui_app_page.hpp"
#include "../model/snake_game_model.hpp"
#include "../model/page_timer_lifecycle.hpp"
#include "../model/game_input_action.hpp"
#include "cp0_keyboard_input_context.hpp"

// ============================================================
//  Snake Game  UIGamePage
//  Screen: 320 x 170  (root_screen_ 320x170)
//
//  Layout:
//    Title bar: 22px with "GAME - Snake" and score
//    Game area: 320 x 148 pixels (40 cols x 18 rows, 8x8 cells)
//
//  Game states:
//    READY     - press ENTER to start
//    PLAYING   - arrow keys to steer, game tick every 200ms
//    GAME_OVER - ENTER to restart, ESC to quit
//
//  Keys:
//    UP/DOWN/LEFT/RIGHT - change direction (no 180-degree reversal)
//    ENTER              - start / restart
//    ESC                - quit to home
// ============================================================
class UIGamePage : public AppPageRoot
{
    // ---- Screen constants ----
    static constexpr int SCREEN_W    = 320;  // Overall screen width
    static constexpr int SCREEN_H    = 170;  // Overall screen height

    // ---- Grid constants ----
    static constexpr int CELL_SIZE   = 8;
    static constexpr int GAME_AREA_W = 320;
    static constexpr int GAME_AREA_H = 148;  // 170 - 22
    static constexpr int TITLE_BAR_H = 22;

    // ---- Colors ----
    static constexpr uint32_t COLOR_BG         = 0x0D1117;
    static constexpr uint32_t COLOR_TITLE_BAR  = 0x1F3A5F;
    static constexpr uint32_t COLOR_ACCENT      = 0x1F6FEB;
    static constexpr uint32_t COLOR_SNAKE_BODY  = 0x2ECC71;
    static constexpr uint32_t COLOR_SNAKE_HEAD  = 0x3DFF85;
    static constexpr uint32_t COLOR_FOOD        = 0xE74C3C;
    static constexpr uint32_t COLOR_TEXT        = 0xE6EDF3;
    static constexpr uint32_t COLOR_TEXT_DIM    = 0x7EA8D8;

    // ---- Game state enum ----
    enum GameState { STATE_READY, STATE_PLAYING, STATE_GAME_OVER };

    // ---- UI objects ----
    lv_obj_t *bg_          = nullptr;
    lv_obj_t *title_bar_   = nullptr;
    lv_obj_t *score_label_ = nullptr;
    lv_obj_t *game_area_   = nullptr;
    lv_obj_t *render_layer_ = nullptr;
    lv_obj_t *overlay_lbl_ = nullptr;   // centered text for READY / GAME_OVER

    // ---- Game timer ----
    PageTimerLifecycle<lv_timer_t *> game_timer_;
    bool game_tick_callback_enabled_ = false;

    // ---- Game data ----
    SnakeGameModel model_;
    GameState state_ = STATE_READY;
    Cp0KeyboardInputContextScope input_context_scope_{KBD_INPUT_CONTEXT_GAME};

public:
    UIGamePage();

    ~UIGamePage();

private:
    // ==================== UI construction ====================
    void create_ui();
    void detach_ui_callbacks();
    static void owned_obj_delete_cb(lv_event_t *event) noexcept;

    // ==================== Overlay text (centered) ====================
    bool show_overlay(const char *text);

    void clear_overlay();

    // ==================== Game logic ====================
    void game_reset();

    void game_start();

    void stop_game_timer() noexcept;

    void game_over();

    void game_tick();

    // ==================== Rendering ====================
    bool render_game();

    bool create_cell(lv_obj_t *parent, int gx, int gy, uint32_t color);

    void update_score_label();

    // ==================== Timer callback ====================
    static void static_game_tick(lv_timer_t *t) noexcept;

    // ==================== Event handling ====================
    void event_handler_init();

    static void static_lvgl_handler(lv_event_t *e) noexcept;

    void event_handler(lv_event_t *e);

    // ---- READY state keys ----
    void handle_ready_key(GameInputAction action);

    // ---- PLAYING state keys ----
    void handle_playing_key(GameInputAction action);

    // ---- GAME_OVER state keys ----
    void handle_gameover_key(GameInputAction action);
};
