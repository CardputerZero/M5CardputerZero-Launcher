#include "zclaw_approval_dialog.h"

#include "zclaw_fonts.hpp"
#include "zclaw_theme.h"
#include "zclaw_widgets.h"

namespace zclaw {

namespace {

lv_coord_t centered_y(lv_coord_t container_height, lv_coord_t item_height)
{
    return (container_height - item_height) / 2;
}

}  // namespace

ApprovalDialog::~ApprovalDialog()
{
    close();
}

void ApprovalDialog::show(const FontManager *fonts,
                          const ApprovalRequest &request,
                          int selected_index)
{
    if (!fonts)
        return;
    close();

    dialog_ = widgets::box(lv_layer_top(), 42, 30, 236, 110, theme::kBar, 8);
    lv_obj_add_event_cb(dialog_, dialog_deleted, LV_EVENT_DELETE, this);
    lv_obj_set_style_border_width(dialog_, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(dialog_, lv_color_hex(theme::kPurple),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_move_foreground(dialog_);

    widgets::box(dialog_, 0, 0, 236, 20, theme::kPanel, 8);
    const lv_coord_t title_height = lv_font_get_line_height(fonts->font_12());
    const lv_coord_t text_height = lv_font_get_line_height(fonts->font_10());
    widgets::label(dialog_, "Permission", 10, centered_y(20, title_height),
                   120, title_height, fonts->font_12(), theme::kText);
    widgets::label(dialog_, "Esc", 194, centered_y(20, text_height),
                   28, text_height, fonts->font_10(), theme::kDim,
                   LV_TEXT_ALIGN_RIGHT);
    widgets::label(dialog_, request.tool, 12, 23, 212, title_height,
                   fonts->font_12(), theme::kText);
    widgets::label(dialog_, request.summary, 12, 42, 212, text_height * 2,
                   fonts->font_10(), theme::kMuted);
    widgets::box(dialog_, 0, 80, 236, 1, theme::kPanelLine);

    static constexpr const char *labels[] = {"Yes", "Always", "No"};
    static constexpr uint32_t colors[] = {theme::kOnline, theme::kPurple, theme::kDim};
    for (int index = 0; index < 3; ++index) {
        buttons_[index] = widgets::box(dialog_, 12 + index * 72, 85, 68, 20,
                                       theme::kPanel, 5);
        lv_obj_set_style_border_width(buttons_[index], 1,
                                      LV_PART_MAIN | LV_STATE_DEFAULT);
        widgets::label(buttons_[index], labels[index], 0,
                       centered_y(20, text_height), 68, text_height,
                       fonts->font_10(), colors[index], LV_TEXT_ALIGN_CENTER);
    }
    update_selection(selected_index);
}

void ApprovalDialog::close()
{
    lv_obj_t *dialog = dialog_;
    if (dialog)
        lv_obj_del(dialog);
    release_dialog();
}

void ApprovalDialog::dialog_deleted(lv_event_t *event)
{
    ApprovalDialog *dialog =
        static_cast<ApprovalDialog *>(lv_event_get_user_data(event));
    if (dialog)
        dialog->release_dialog();
}

void ApprovalDialog::release_dialog()
{
    dialog_ = nullptr;
    for (lv_obj_t *&button : buttons_)
        button = nullptr;
}

void ApprovalDialog::update_selection(int selected_index)
{
    for (int index = 0; index < 3; ++index) {
        if (!buttons_[index])
            continue;
        const bool selected = index == selected_index;
        lv_obj_set_style_bg_color(buttons_[index],
                                  lv_color_hex(selected ? theme::kSelectedPanel
                                                        : theme::kPanel),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_color(buttons_[index],
                                      lv_color_hex(selected ? theme::kText : theme::kPanelLine),
                                      LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

bool ApprovalDialog::is_open() const
{
    return dialog_ != nullptr;
}

}  // namespace zclaw
