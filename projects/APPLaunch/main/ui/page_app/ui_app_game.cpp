/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#define APP_PAGE_IMPLEMENTATION_UNIT
#include "ui_app_game.hpp"
#include "../model/snake_view_contract.hpp"

#include "input_keys.h"

#include <cstdio>

UIGamePage::UIGamePage() : AppPageRoot()
{

    create_ui();
    event_handler_init();
}

UIGamePage::~UIGamePage()
{
    if (root_screen_)
        lv_obj_remove_event_cb_with_user_data(
            root_screen_, UIGamePage::static_lvgl_handler, this);
    stop_game_timer();
    detach_ui_callbacks();
}

void UIGamePage::game_reset()
{
    model_.reset();
    update_score_label();
}

void UIGamePage::game_start()
{
    if (!game_area_) return;
    state_ = STATE_PLAYING;
    clear_overlay();
    game_reset();
    if (!render_game()) {
        state_ = STATE_READY;
        show_overlay("Unable to render game");
        return;
    }

    game_tick_callback_enabled_ = game_timer_.start(
        [this] { return lv_timer_create(UIGamePage::static_game_tick, 200, this); },
        [](lv_timer_t *timer) { lv_timer_delete(timer); });
    if (!game_tick_callback_enabled_) {
        state_ = STATE_READY;
        show_overlay("Unable to start game");
    }
}

void UIGamePage::stop_game_timer() noexcept
{
    game_tick_callback_enabled_ = false;
    game_timer_.stop();
}

void UIGamePage::game_over()
{
    state_ = STATE_GAME_OVER;
    stop_game_timer();

    char buf[80];
    snprintf(buf, sizeof(buf), "Game Over! Score: %d\nOK: Restart  ESC: Quit", model_.score());
    show_overlay(buf);
}

void UIGamePage::game_tick()
{
    if (state_ != STATE_PLAYING) return;

    const int previous_score = model_.score();
    if (!model_.tick()) {
        game_over();
        return;
    }
    if (model_.score() != previous_score) update_score_label();
    if (!render_game()) {
        stop_game_timer();
        state_ = STATE_READY;
        show_overlay("Unable to render game");
    }
}

void UIGamePage::static_game_tick(lv_timer_t *t) noexcept
{
    UIGamePage *self = nullptr;
    try {
        self = static_cast<UIGamePage *>(lv_timer_get_user_data(t));
        if (!self || !self->game_tick_callback_enabled_) return;
        if (!snake_tick_callback_allowed(
                self->game_tick_callback_enabled_, self->game_timer_.current(t),
                self->state_ == STATE_PLAYING, self->game_area_,
                self->render_layer_)) return;
        self->game_tick();
    } catch (...) {
        if (self) {
            self->game_tick_callback_enabled_ = false;
            self->state_ = STATE_READY;
        }
    }
}

void UIGamePage::event_handler_init()
{
    if (root_screen_)
        lv_obj_add_event_cb(root_screen_, UIGamePage::static_lvgl_handler, LV_EVENT_ALL, this);
}

void UIGamePage::static_lvgl_handler(lv_event_t *e) noexcept
{
    UIGamePage *self = nullptr;
    try {
        if (!e) return;
        self = static_cast<UIGamePage *>(lv_event_get_user_data(e));
        if (!self || !snake_page_event_callback_allowed(
                lv_event_get_current_target(e), self->root_screen_)) return;
        self->event_handler(e);
    } catch (...) {
        if (self) {
            self->game_tick_callback_enabled_ = false;
            self->state_ = STATE_READY;
        }
    }
}

void UIGamePage::event_handler(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_DELETE &&
        lv_event_get_target(e) == lv_event_get_current_target(e)) {
        stop_game_timer();
        bg_ = nullptr;
        title_bar_ = nullptr;
        score_label_ = nullptr;
        game_area_ = nullptr;
        render_layer_ = nullptr;
        overlay_lbl_ = nullptr;
        return;
    }
    if (launcher_ui::events::is_key_released(e))
    {
        const GameInputAction action =
            game_input_action(launcher_ui::events::keyboard_item(e));

        switch (state_)
        {
        case STATE_READY:
            handle_ready_key(action);
            break;
        case STATE_PLAYING:
            handle_playing_key(action);
            break;
        case STATE_GAME_OVER:
            handle_gameover_key(action);
            break;
        }
    }
}

void UIGamePage::handle_ready_key(GameInputAction action)
{
    switch (action) {
    case GameInputAction::CONFIRM:
        game_start();
        break;
    case GameInputAction::CANCEL:
        if (navigate_home) navigate_home();
        break;
    default:
        break;
    }
}

void UIGamePage::handle_playing_key(GameInputAction action)
{
    switch (action) {
    case GameInputAction::UP:
        model_.queue_direction(SnakeGameModel::Direction::UP);
        break;
    case GameInputAction::DOWN:
        model_.queue_direction(SnakeGameModel::Direction::DOWN);
        break;
    case GameInputAction::LEFT:
        model_.queue_direction(SnakeGameModel::Direction::LEFT);
        break;
    case GameInputAction::RIGHT:
        model_.queue_direction(SnakeGameModel::Direction::RIGHT);
        break;
    case GameInputAction::CANCEL:
        // Pause and quit
        stop_game_timer();
        state_ = STATE_READY;
        if (game_area_) lv_obj_clean(game_area_);
        render_layer_ = nullptr;
        overlay_lbl_ = nullptr;
        show_overlay("Classic Snake game.\n\nF / X / Z / C: move");
        model_.reset();
        update_score_label();
        break;
    default:
        break;
    }
}

void UIGamePage::handle_gameover_key(GameInputAction action)
{
    switch (action) {
    case GameInputAction::CONFIRM:
        clear_overlay();
        game_start();
        break;
    case GameInputAction::CANCEL:
        if (navigate_home) navigate_home();
        break;
    default:
        break;
    }
}
