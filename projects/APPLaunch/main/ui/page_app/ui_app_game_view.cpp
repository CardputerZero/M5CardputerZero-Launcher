#define APP_PAGE_IMPLEMENTATION_UNIT
#include "ui_app_game.hpp"
#include "../model/snake_view_contract.hpp"

#include <cstdio>

void UIGamePage::create_ui()
{
    if (!root_screen_) return;
    bg_ = lv_obj_create(root_screen_);
    if (!bg_) return;
    auto rollback = [this]() {
        lv_obj_t *background = bg_;
        bg_ = nullptr;
        title_bar_ = nullptr;
        score_label_ = nullptr;
        game_area_ = nullptr;
        render_layer_ = nullptr;
        overlay_lbl_ = nullptr;
        if (background) lv_obj_delete(background);
    };
    lv_obj_add_event_cb(bg_, owned_obj_delete_cb, LV_EVENT_DELETE, this);
    lv_obj_set_size(bg_, SCREEN_W, SCREEN_H);
    lv_obj_set_pos(bg_, 0, 0);
    lv_obj_set_style_radius(bg_, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bg_, lv_color_hex(COLOR_BG), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bg_, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bg_, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(bg_, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(bg_, LV_OBJ_FLAG_SCROLLABLE);

    title_bar_ = lv_obj_create(bg_);
    if (!title_bar_) {
        rollback();
        return;
    }
    lv_obj_add_event_cb(title_bar_, owned_obj_delete_cb, LV_EVENT_DELETE, this);
    {
        lv_obj_set_size(title_bar_, SCREEN_W, TITLE_BAR_H);
        lv_obj_set_pos(title_bar_, 0, 0);
        lv_obj_set_style_radius(title_bar_, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(title_bar_, lv_color_hex(COLOR_TITLE_BAR),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(title_bar_, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(title_bar_, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_left(title_bar_, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_clear_flag(title_bar_, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *title = lv_label_create(title_bar_);
        if (!title) {
            rollback();
            return;
        }
        {
            lv_label_set_text(title, "GAME - Snake");
            lv_obj_set_align(title, LV_ALIGN_LEFT_MID);
            lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF),
                                        LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_font(title, &lv_font_montserrat_14,
                                       LV_PART_MAIN | LV_STATE_DEFAULT);
        }

        score_label_ = lv_label_create(title_bar_);
        if (!score_label_) {
            rollback();
            return;
        }
        {
            lv_obj_add_event_cb(score_label_, owned_obj_delete_cb, LV_EVENT_DELETE, this);
            lv_label_set_text(score_label_, "Score: 0");
            lv_obj_set_align(score_label_, LV_ALIGN_RIGHT_MID);
            lv_obj_set_x(score_label_, -8);
            lv_obj_set_style_text_color(score_label_, lv_color_hex(COLOR_TEXT_DIM),
                                        LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_font(score_label_, &lv_font_montserrat_12,
                                       LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    }

    game_area_ = lv_obj_create(bg_);
    if (!game_area_) {
        rollback();
        return;
    }
    lv_obj_add_event_cb(game_area_, owned_obj_delete_cb, LV_EVENT_DELETE, this);
    lv_obj_set_size(game_area_, GAME_AREA_W, GAME_AREA_H);
    lv_obj_set_pos(game_area_, 0, TITLE_BAR_H);
    lv_obj_set_style_radius(game_area_, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(game_area_, lv_color_hex(COLOR_BG), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(game_area_, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(game_area_, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(game_area_, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(game_area_, LV_OBJ_FLAG_SCROLLABLE);
    if (!show_overlay("F/X/Z/C: Move\nOK: Start  ESC: Quit")) rollback();
}

bool UIGamePage::show_overlay(const char *text)
{
    if (!game_area_) return false;
    clear_overlay();
    overlay_lbl_ = lv_label_create(game_area_);
    if (!overlay_lbl_) return false;
    lv_obj_add_event_cb(overlay_lbl_, owned_obj_delete_cb, LV_EVENT_DELETE, this);
    lv_label_set_text(overlay_lbl_, text);
    lv_obj_set_align(overlay_lbl_, LV_ALIGN_CENTER);
    lv_obj_set_style_text_color(overlay_lbl_, lv_color_hex(COLOR_TEXT), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(overlay_lbl_, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(overlay_lbl_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    return true;
}

void UIGamePage::clear_overlay()
{
    if (!overlay_lbl_) return;
    lv_obj_delete(overlay_lbl_);
    overlay_lbl_ = nullptr;
}

bool UIGamePage::render_game()
{
    if (!game_area_) {
        game_timer_.stop();
        state_ = STATE_READY;
        return false;
    }
    lv_obj_t *new_layer = lv_obj_create(game_area_);
    if (!new_layer) return false;
    lv_obj_remove_style_all(new_layer);
    lv_obj_add_event_cb(new_layer, owned_obj_delete_cb, LV_EVENT_DELETE, this);
    lv_obj_set_size(new_layer, GAME_AREA_W, GAME_AREA_H);
    lv_obj_set_pos(new_layer, 0, 0);
    lv_obj_clear_flag(new_layer, LV_OBJ_FLAG_SCROLLABLE);

    const auto food = model_.food();
    const auto &snake = model_.snake();
    SnakeFrameBuildContract build(snake.size() + 1);
    if (!create_cell(new_layer, food.x, food.y, COLOR_FOOD)) {
        lv_obj_delete(new_layer);
        return false;
    }
    build.cell_created();
    for (size_t index = 1; index < snake.size(); ++index) {
        if (!create_cell(new_layer, snake[index].x, snake[index].y, COLOR_SNAKE_BODY)) {
            lv_obj_delete(new_layer);
            return false;
        }
        build.cell_created();
    }
    if (!snake.empty()) {
        if (!create_cell(new_layer, snake.front().x, snake.front().y, COLOR_SNAKE_HEAD)) {
            lv_obj_delete(new_layer);
            return false;
        }
        build.cell_created();
    }
    if (!build.ready()) {
        lv_obj_delete(new_layer);
        return false;
    }
    clear_overlay();
    if (render_layer_) lv_obj_delete(render_layer_);
    render_layer_ = new_layer;
    return true;
}

bool UIGamePage::create_cell(lv_obj_t *parent, int grid_x, int grid_y, uint32_t color)
{
    if (!parent) return false;
    lv_obj_t *cell = lv_obj_create(parent);
    if (!cell) return false;
    lv_obj_set_size(cell, 7, 7);
    lv_obj_set_pos(cell, grid_x * CELL_SIZE, grid_y * CELL_SIZE);
    lv_obj_set_style_radius(cell, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(cell, lv_color_hex(color), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(cell, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(cell, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(cell, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(cell, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    return true;
}

void UIGamePage::detach_ui_callbacks()
{
    lv_obj_t *objects[] = {
        overlay_lbl_, render_layer_, score_label_, game_area_, title_bar_, bg_};
    for (lv_obj_t *object : objects)
        if (object)
            lv_obj_remove_event_cb_with_user_data(object, owned_obj_delete_cb, this);
}

void UIGamePage::owned_obj_delete_cb(lv_event_t *event) noexcept
{
    try {
    if (!event || !snake_owned_delete_callback_allowed(
            lv_event_get_target(event), lv_event_get_current_target(event))) return;
    auto *self = static_cast<UIGamePage *>(lv_event_get_user_data(event));
    auto *deleted = static_cast<lv_obj_t *>(lv_event_get_target(event));
    if (!self || !deleted) return;
    if (self->overlay_lbl_ == deleted) self->overlay_lbl_ = nullptr;
    if (self->render_layer_ == deleted) self->render_layer_ = nullptr;
    if (self->score_label_ == deleted) self->score_label_ = nullptr;
    if (self->game_area_ == deleted) {
        self->game_area_ = nullptr;
        self->render_layer_ = nullptr;
        self->overlay_lbl_ = nullptr;
        self->game_timer_.stop();
        self->state_ = STATE_READY;
    }
    if (self->title_bar_ == deleted) {
        self->title_bar_ = nullptr;
        self->score_label_ = nullptr;
    }
    if (self->bg_ == deleted) {
        self->bg_ = nullptr;
        self->title_bar_ = nullptr;
        self->score_label_ = nullptr;
        self->game_area_ = nullptr;
        self->render_layer_ = nullptr;
        self->overlay_lbl_ = nullptr;
        self->game_timer_.stop();
        self->state_ = STATE_READY;
    }
    } catch (...) {
        auto *self = event
            ? static_cast<UIGamePage *>(lv_event_get_user_data(event)) : nullptr;
        if (self) self->state_ = STATE_READY;
    }
}

void UIGamePage::update_score_label()
{
    if (!score_label_) return;
    char text[32];
    std::snprintf(text, sizeof(text), "Score: %d", model_.score());
    lv_label_set_text(score_label_, text);
}
