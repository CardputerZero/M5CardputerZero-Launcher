#define APP_PAGE_IMPLEMENTATION_UNIT
#include "ui_app_tank_battle.hpp"

void UITankBattlePage::creat_UI()
{
    if (!root_screen_) return;
    lv_obj_t *bg = lv_obj_create(root_screen_);
    if (!bg) return;
    background_ = bg;
    lv_obj_add_event_cb(bg, owned_obj_delete_cb, LV_EVENT_DELETE, this);
    lv_obj_set_size(bg, SCREEN_W, SCREEN_H);
    lv_obj_set_pos(bg, 0, 0);
    lv_obj_set_style_radius(bg, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bg, lv_color_hex(0x10151C), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bg, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bg, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(bg, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(bg, LV_OBJ_FLAG_SCROLLABLE);
    ui_obj_["bg"] = bg;

    lv_obj_t *title = lv_obj_create(bg);
    if (!title) return;
    lv_obj_set_size(title, SCREEN_W, 24);
    lv_obj_set_pos(title, 0, 0);
    lv_obj_set_style_radius(title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(title, lv_color_hex(0x2A4D69), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(title, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(title, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(title, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(title, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title_label = lv_label_create(title);
    if (title_label) {
        lv_label_set_text(title_label, "Tank Game");
        lv_obj_set_align(title_label, LV_ALIGN_LEFT_MID);
        lv_obj_set_style_text_color(title_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(title_label, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    }

    lv_obj_t *hint_label = lv_label_create(title);
    if (hint_label) {
        lv_label_set_text(hint_label, "Classic tank game inspired by Battle City.\n\nF / X / Z / C: move\nSpace: fire");
        lv_obj_set_align(hint_label, LV_ALIGN_RIGHT_MID);
        lv_obj_set_x(hint_label, -4);
        lv_obj_set_style_text_color(hint_label, lv_color_hex(0xB7D1E6), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(hint_label, &lv_font_montserrat_10, LV_PART_MAIN | LV_STATE_DEFAULT);
    }

    lv_obj_t *status = lv_label_create(bg);
    if (!status) return;
    status_label_ = status;
    lv_obj_add_event_cb(status, owned_obj_delete_cb, LV_EVENT_DELETE, this);
    lv_label_set_text(status, "Score:0  Enemy:5");
    lv_obj_set_pos(status, 8, 26);
    lv_obj_set_width(status, 304);
    lv_label_set_long_mode(status, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(status, lv_color_hex(0xDDE6ED), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(status, &lv_font_montserrat_12, LV_PART_MAIN | LV_STATE_DEFAULT);
    ui_obj_["status"] = status;

    lv_obj_t *arena = lv_obj_create(bg);
    if (!arena) return;
    arena_ = arena;
    lv_obj_add_event_cb(arena, owned_obj_delete_cb, LV_EVENT_DELETE, this);
    lv_obj_set_size(arena, ARENA_W, ARENA_H);
    lv_obj_set_pos(arena, ARENA_X, ARENA_Y);
    lv_obj_set_style_radius(arena, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(arena, lv_color_hex(0x0B0F14), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(arena, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(arena, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(arena, lv_color_hex(0x2F3F4F), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(arena, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(arena, LV_OBJ_FLAG_SCROLLABLE);
    ui_obj_["arena"] = arena;

    for (int x = 1; x < GRID_COLS; ++x) {
        lv_obj_t *line = lv_obj_create(arena);
        if (!line) continue;
        lv_obj_set_size(line, 1, GRID_H);
        lv_obj_set_pos(line, GRID_OX + x * CELL, GRID_OY);
        lv_obj_set_style_radius(line, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(line, lv_color_hex(0x18222D), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(line, 120, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(line, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_clear_flag(line, LV_OBJ_FLAG_SCROLLABLE);
    }

    for (int y = 1; y < GRID_ROWS; ++y) {
        lv_obj_t *line = lv_obj_create(arena);
        if (!line) continue;
        lv_obj_set_size(line, GRID_W, 1);
        lv_obj_set_pos(line, GRID_OX, GRID_OY + y * CELL);
        lv_obj_set_style_radius(line, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(line, lv_color_hex(0x18222D), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(line, 120, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(line, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_clear_flag(line, LV_OBJ_FLAG_SCROLLABLE);
    }

    player_tank_ = create_tank_visual(arena, 0x35D07F);
    player_obj_ = player_tank_.root;
    if (!player_obj_) return;
    lv_obj_add_event_cb(player_obj_, owned_obj_delete_cb, LV_EVENT_DELETE, this);

    enemy_tanks_.clear();
    for (size_t i = 0; i < model_.enemies().size(); ++i) {
        TankVisual visual = create_tank_visual(arena, 0xE65045);
        if (visual.root)
            lv_obj_add_event_cb(visual.root, owned_obj_delete_cb, LV_EVENT_DELETE, this);
        enemy_tanks_.push_back(visual);
    }

    bullet_objs_.clear();
    for (size_t i = 0; i < model_.bullets().size(); ++i) {
        lv_obj_t *obj = lv_obj_create(arena);
        if (!obj) {
            bullet_objs_.push_back(nullptr);
            continue;
        }
        lv_obj_add_event_cb(obj, owned_obj_delete_cb, LV_EVENT_DELETE, this);
        lv_obj_set_size(obj, 4, 4);
        lv_obj_set_style_radius(obj, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(obj, lv_color_hex(0xF4D03F), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(obj, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
        bullet_objs_.push_back(obj);
    }

    create_game_message_panel(arena);
    sync_scene();
}

UITankBattlePage::TankVisual UITankBattlePage::create_tank_visual(
    lv_obj_t *arena, std::uint32_t armor_color)
{
    TankVisual visual;
    visual.root = lv_obj_create(arena);
    if (!visual.root) return visual;
    lv_obj_set_size(visual.root, CELL - 2, CELL - 2);
    lv_obj_set_style_bg_opa(visual.root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(visual.root, 0, 0);
    lv_obj_set_style_pad_all(visual.root, 0, 0);
    lv_obj_clear_flag(visual.root, LV_OBJ_FLAG_SCROLLABLE);

    auto make_part = [&](int x, int y, int w, int h, std::uint32_t color,
                         int radius) -> lv_obj_t * {
        lv_obj_t *part = lv_obj_create(visual.root);
        if (!part) return nullptr;
        lv_obj_set_size(part, w, h);
        lv_obj_set_pos(part, x, y);
        lv_obj_set_style_radius(part, radius, 0);
        lv_obj_set_style_bg_color(part, lv_color_hex(color), 0);
        lv_obj_set_style_bg_opa(part, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(part, 0, 0);
        lv_obj_set_style_pad_all(part, 0, 0);
        lv_obj_clear_flag(part, LV_OBJ_FLAG_SCROLLABLE);
        return part;
    };

    visual.left_track = make_part(0, 2, 2, 9, 0x252B31, 1);
    visual.right_track = make_part(10, 2, 2, 9, 0x252B31, 1);
    visual.hull = make_part(2, 2, 8, 9, armor_color, 2);
    visual.barrel = make_part(5, 0, 2, 5, 0xD8E0E5, 1);
    visual.turret = make_part(4, 4, 4, 4, armor_color, 2);
    return visual;
}

void UITankBattlePage::sync_tank_visual(TankVisual &visual, TankDirection direction,
                                        std::uint32_t armor_color)
{
    if (!visual.root) return;
    if (visual.hull) lv_obj_set_style_bg_color(visual.hull, lv_color_hex(armor_color), 0);
    if (visual.turret) lv_obj_set_style_bg_color(visual.turret, lv_color_hex(armor_color), 0);
    if (!visual.barrel) return;

    switch (direction) {
    case TankDirection::UP:
    case TankDirection::DOWN:
        if (visual.left_track) {
            lv_obj_set_size(visual.left_track, 2, 9);
            lv_obj_set_pos(visual.left_track, 0, 2);
        }
        if (visual.right_track) {
            lv_obj_set_size(visual.right_track, 2, 9);
            lv_obj_set_pos(visual.right_track, 10, 2);
        }
        if (visual.hull) {
            lv_obj_set_size(visual.hull, 8, 9);
            lv_obj_set_pos(visual.hull, 2, 2);
        }
        if (direction == TankDirection::UP) {
            lv_obj_set_size(visual.barrel, 2, 5);
            lv_obj_set_pos(visual.barrel, 5, 0);
        } else {
            lv_obj_set_size(visual.barrel, 2, 5);
            lv_obj_set_pos(visual.barrel, 5, 7);
        }
        break;
    case TankDirection::LEFT:
    case TankDirection::RIGHT:
        if (visual.left_track) {
            lv_obj_set_size(visual.left_track, 9, 2);
            lv_obj_set_pos(visual.left_track, 2, 0);
        }
        if (visual.right_track) {
            lv_obj_set_size(visual.right_track, 9, 2);
            lv_obj_set_pos(visual.right_track, 2, 10);
        }
        if (visual.hull) {
            lv_obj_set_size(visual.hull, 9, 8);
            lv_obj_set_pos(visual.hull, 2, 2);
        }
        if (direction == TankDirection::LEFT) {
            lv_obj_set_size(visual.barrel, 5, 2);
            lv_obj_set_pos(visual.barrel, 0, 5);
        } else {
            lv_obj_set_size(visual.barrel, 5, 2);
            lv_obj_set_pos(visual.barrel, 7, 5);
        }
        break;
    }
    if (visual.turret) lv_obj_move_foreground(visual.turret);
}

void UITankBattlePage::create_game_message_panel(lv_obj_t *arena)
{
    if (!arena) return;
    game_msg_panel_ = lv_obj_create(arena);
    if (!game_msg_panel_) return;
    lv_obj_add_event_cb(game_msg_panel_, owned_obj_delete_cb, LV_EVENT_DELETE, this);
    lv_obj_set_size(game_msg_panel_, 224, 66);
    lv_obj_set_pos(game_msg_panel_, (ARENA_W - 224) / 2, (ARENA_H - 66) / 2);
    lv_obj_set_style_radius(game_msg_panel_, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(game_msg_panel_, lv_color_hex(0x111827), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(game_msg_panel_, 235, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(game_msg_panel_, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(game_msg_panel_, lv_color_hex(0xF4D03F), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(game_msg_panel_, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(game_msg_panel_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(game_msg_panel_, LV_OBJ_FLAG_HIDDEN);

    game_msg_label_ = lv_label_create(game_msg_panel_);
    if (!game_msg_label_) return;
    lv_obj_add_event_cb(game_msg_label_, owned_obj_delete_cb, LV_EVENT_DELETE, this);
    lv_obj_set_width(game_msg_label_, 210);
    lv_label_set_long_mode(game_msg_label_, LV_LABEL_LONG_WRAP);
    lv_label_set_text(game_msg_label_, "");
    lv_obj_set_style_text_align(game_msg_label_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(game_msg_label_, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(game_msg_label_, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(game_msg_label_);
}
