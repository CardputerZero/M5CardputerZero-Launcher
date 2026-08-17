#define APP_PAGE_IMPLEMENTATION_UNIT
#include "../ui_app_setup.hpp"
#include "setup_page_access.hpp"

namespace setting {

void BluetoothUiSession::build_alias_view(UISetupPage &page)
{
    SetupPageAccess access(page);
    lv_obj_t *container = access.content_container();
    lv_obj_clean(container);
    alias_input_lbl_ = nullptr;
    alias_hint_lbl_ = nullptr;

    lv_obj_t *title = lv_label_create(container);
    lv_label_set_text(title, "Bluetooth Name");
    lv_obj_set_pos(title, 10, 10);
    lv_obj_set_style_text_color(title, lv_color_hex(0x58A6FF), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, LV_PART_MAIN);

    lv_obj_t *label = lv_label_create(container);
    lv_label_set_text(label, "Name:");
    lv_obj_set_pos(label, 10, 38);
    lv_obj_set_style_text_color(label, lv_color_hex(0xCCCCCC), LV_PART_MAIN);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, LV_PART_MAIN);

    alias_input_lbl_ = lv_label_create(container);
    lv_obj_set_pos(alias_input_lbl_, 64, 36);
    lv_obj_set_width(alias_input_lbl_, 236);
    lv_label_set_long_mode(alias_input_lbl_, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_color(alias_input_lbl_, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(alias_input_lbl_, &lv_font_montserrat_14, LV_PART_MAIN);
    alias_update_display();

    alias_hint_lbl_ = lv_label_create(container);
    lv_label_set_text(alias_hint_lbl_, "OK:set  BS:del  ESC:cancel");
    lv_obj_set_pos(alias_hint_lbl_, 10, access.content_height() - 14);
    lv_obj_set_style_text_color(alias_hint_lbl_, lv_color_hex(0x555555), LV_PART_MAIN);
    lv_obj_set_style_text_font(alias_hint_lbl_, &lv_font_montserrat_10, LV_PART_MAIN);
}

void BluetoothUiSession::alias_update_display()
{
    if (!alias_input_lbl_) return;
    const std::string display = model_.alias_input() + "_";
    lv_label_set_text(alias_input_lbl_, display.c_str());
}

void BluetoothUiSession::build_list(UISetupPage &page)
{
    SetupPageAccess access(page);
    const SetupLayout layout = access.layout();
    lv_obj_t *container = access.content_container();
    lv_obj_clean(container);
    rebuild_rows();

    const cp0_bt_status_t status = get_status();
    char title_text[96];
    snprintf(title_text, sizeof(title_text), "%s: %s  %.24s",
             model_.list_mode() == BluetoothListMode::SCAN ? "Scan" : "Connected",
             status.powered ? "On" : "Off", status.address[0] ? status.address : "--");
    lv_obj_t *title = lv_label_create(container);
    lv_label_set_text(title, title_text);
    lv_obj_set_pos(title, 8, 2);
    lv_obj_set_style_text_color(title, lv_color_hex(0x58A6FF), LV_PART_MAIN);
    lv_obj_set_style_text_font(
        title,
        launcher_fonts().get("Montserrat-Bold.ttf", 12, LV_FREETYPE_FONT_STYLE_BOLD),
        LV_PART_MAIN);
    access.apply_overflow(title, 8, layout.wifi_title_width, true);

    const auto &rows = model_.rows();
    if (rows.empty()) {
        lv_obj_t *empty = lv_label_create(container);
        if (!status.powered)
            lv_label_set_text(empty, "Bluetooth is off. Enable Power first.");
        else if (model_.list_mode() == BluetoothListMode::SCAN)
            lv_label_set_text(empty, "Scanning...");
        else
            lv_label_set_text(empty, "No connected devices.");
        lv_obj_set_pos(empty, 8, 50);
        lv_obj_set_width(empty, 300);
        lv_label_set_long_mode(empty, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_color(empty, lv_color_hex(0x666666), LV_PART_MAIN);
        lv_obj_set_style_text_font(empty, &lv_font_montserrat_12, LV_PART_MAIN);
    }

    constexpr int list_y = 22;
    constexpr int row_step = 20;
    const int hint_y = layout.content_height - 14;
    constexpr int list_bottom_gap = 8;
    int visible_count = (hint_y - list_bottom_gap - list_y) / row_step;
    if (visible_count < 1) visible_count = 1;
    int offset = model_.list_selection() - visible_count / 2;
    if (offset < 0) offset = 0;
    if (offset > static_cast<int>(rows.size()) - visible_count)
        offset = static_cast<int>(rows.size()) - visible_count;
    if (offset < 0) offset = 0;

    for (int visible_index = 0;
         visible_index < visible_count && visible_index + offset < static_cast<int>(rows.size());
         ++visible_index) {
        const int row_index = visible_index + offset;
        const BluetoothListRow &row = rows[row_index];
        const int y = list_y + visible_index * row_step;

        if (row.is_header) {
            lv_obj_t *header = lv_label_create(container);
            lv_label_set_text(header, row.title);
            lv_obj_set_pos(header, 8, y + 3);
            lv_obj_set_style_text_color(header, lv_color_hex(0x888888), LV_PART_MAIN);
            lv_obj_set_style_text_font(
                header,
                launcher_fonts().get("Montserrat-Bold.ttf", 10, LV_FREETYPE_FONT_STYLE_BOLD),
                LV_PART_MAIN);
            continue;
        }

        const bool selected = row_index == model_.list_selection();
        const cp0_bt_device_t &device = devices_[row.device_index];
        if (selected) {
            lv_obj_t *background = lv_obj_create(container);
            lv_obj_set_size(background, layout.screen_width - 8, 20);
            lv_obj_set_pos(background, 4, y);
            lv_obj_set_style_radius(background, 2, LV_PART_MAIN);
            lv_obj_set_style_bg_color(background, lv_color_hex(0x1F3A5F), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(background, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_border_width(background, 0, LV_PART_MAIN);
            lv_obj_clear_flag(background, LV_OBJ_FLAG_SCROLLABLE);
        }

        const uint32_t text_color =
            device.connected ? 0x58A6FF : (selected ? 0xFFFFFF : 0xCCCCCC);
        lv_obj_t *name = lv_label_create(container);
        lv_label_set_text(name, device.name[0] ? device.name : device.address);
        lv_obj_set_pos(name, 8, y + 1);
        lv_obj_set_width(name, 150);
        lv_label_set_long_mode(name, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_color(name, lv_color_hex(text_color), LV_PART_MAIN);
        lv_obj_set_style_text_font(name, &lv_font_montserrat_12, LV_PART_MAIN);

        lv_obj_t *address = lv_label_create(container);
        lv_label_set_text(address, device.address);
        lv_obj_set_pos(address, 8, y + 12);
        lv_obj_set_width(address, 190);
        lv_label_set_long_mode(address, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_color(address, lv_color_hex(selected ? 0xBBBBBB : 0x777777),
                                    LV_PART_MAIN);
        lv_obj_set_style_text_font(address, &lv_font_montserrat_10, LV_PART_MAIN);

        char state_text[32];
        if (device.connected)
            snprintf(state_text, sizeof(state_text), "Connected");
        else if (device.paired)
            snprintf(state_text, sizeof(state_text), "Paired");
        else
            snprintf(state_text, sizeof(state_text), "%d", device.rssi);
        lv_obj_t *state = lv_label_create(container);
        lv_label_set_text(state, state_text);
        lv_obj_set_pos(state, 226, y + 4);
        lv_obj_set_width(state, 88);
        lv_label_set_long_mode(state, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_align(state, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
        lv_obj_set_style_text_color(state, lv_color_hex(text_color), LV_PART_MAIN);
        lv_obj_set_style_text_font(state, &lv_font_montserrat_10, LV_PART_MAIN);
    }

    lv_obj_t *hint = lv_label_create(container);
    lv_label_set_text(hint, model_.list_mode() == BluetoothListMode::SCAN
                                ? "OK:act R:restart ESC:back"
                                : "OK:disconnect D:remove ESC:back");
    lv_obj_set_pos(hint, 8, hint_y);
    lv_obj_set_width(hint, 304);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x555555), LV_PART_MAIN);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, LV_PART_MAIN);
}

void BluetoothUiSession::show_action(UISetupPage &page, const char *message, uint32_t color)
{
    lv_obj_t *container = SetupPageAccess(page).content_container();
    lv_obj_clean(container);
    lv_obj_t *label = lv_label_create(container);
    lv_label_set_text(label, message);
    lv_obj_set_pos(label, 8, 60);
    lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_refr_now(nullptr);
}

bool BluetoothUiSession::show_power_warning(UISetupPage &page)
{
    SetupPageAccess access(page);
    lv_obj_t *container = access.content_container();
    if (!container) return false;
    lv_obj_clean(container);

    lv_obj_t *dialog = lv_obj_create(container);
    if (!dialog) return false;
    lv_obj_set_size(dialog, 280, 92);
    lv_obj_center(dialog);
    lv_obj_set_style_radius(dialog, 4, LV_PART_MAIN);
    lv_obj_set_style_border_width(dialog, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(dialog, lv_color_hex(0xFFAA00), LV_PART_MAIN);
    lv_obj_set_style_bg_color(dialog, lv_color_hex(0x171717), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(dialog, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(dialog, 0, LV_PART_MAIN);
    lv_obj_clear_flag(dialog, LV_OBJ_FLAG_SCROLLABLE);

    auto create_dialog_label = [dialog](const char *text, int x, int y,
                                        uint32_t color, const lv_font_t *font) {
        lv_obj_t *label = lv_label_create(dialog);
        if (!label) return static_cast<lv_obj_t *>(nullptr);
        lv_label_set_text(label, text);
        lv_obj_set_pos(label, x, y);
        lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
        lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
        return label;
    };
    if (!create_dialog_label("Bluetooth power is off", 12, 10, 0xFFAA00,
                             &lv_font_montserrat_14) ||
        !create_dialog_label("Turn on Power before continuing.", 12, 36, 0xCCCCCC,
                             &lv_font_montserrat_12) ||
        !create_dialog_label("OK", 246, 68, 0x58A6FF, &lv_font_montserrat_12)) {
        lv_obj_clean(container);
        return false;
    }
    lv_refr_now(nullptr);
    return true;
}

} // namespace setting
