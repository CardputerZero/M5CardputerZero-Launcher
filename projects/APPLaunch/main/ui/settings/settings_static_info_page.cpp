#include "settings_static_info_page.hpp"
#include "settings_fonts.hpp"

#include "cp0_font_service.hpp"

#include <utility>

namespace {

#define SETTINGS_T12B_STRINGIFY_IMPL(value) #value
#define SETTINGS_T12B_STRINGIFY(value) SETTINGS_T12B_STRINGIFY_IMPL(value)

const char *build_version()
{
#ifdef LAUNCHER_VERSION_RAW
    return SETTINGS_T12B_STRINGIFY(LAUNCHER_VERSION_RAW);
#else
    return "unknown";
#endif
}

const char *build_date()
{
#ifdef LAUNCHER_BUILD_DATE_RAW
    return SETTINGS_T12B_STRINGIFY(LAUNCHER_BUILD_DATE_RAW);
#else
    return "unknown";
#endif
}

const char *build_channel()
{
#ifdef LAUNCHER_CHANNEL_RAW
    return SETTINGS_T12B_STRINGIFY(LAUNCHER_CHANNEL_RAW);
#else
    return "unknown";
#endif
}

const char *build_commit()
{
#ifdef LAUNCHER_GIT_COMMIT_RAW
    return SETTINGS_T12B_STRINGIFY(LAUNCHER_GIT_COMMIT_RAW);
#else
    return "unknown";
#endif
}

#undef SETTINGS_T12B_STRINGIFY
#undef SETTINGS_T12B_STRINGIFY_IMPL

} // namespace

LvSettingStaticInfoPage3::LvSettingStaticInfoPage3() = default;

LvSettingStaticInfoPage3::LvSettingStaticInfoPage3(
    lv_obj_t *parent,
    const NodeIter &page_node,
    std::function<void()> back_callback,
    settings_t12b::about_help::Content content)
    : page_node_(page_node), content_(std::move(content))
{
    LeaveSelfPage = std::move(back_callback);
    create_ui(parent);
}

LvSettingStaticInfoPage3::~LvSettingStaticInfoPage3()
{
    if (ComponensObj) {
        lv_obj_delete(ComponensObj);
        ComponensObj = nullptr;
    }
}

void LvSettingStaticInfoPage3::AnimateNextIn(std::function<void()> callback)
{
    if (callback) callback();
}

void LvSettingStaticInfoPage3::AnimateNextOut(std::function<void()> callback)
{
    if (callback) callback();
}

void LvSettingStaticInfoPage3::LoadNextPage() {}

void LvSettingStaticInfoPage3::LeaveNextPage()
{
    if (LeaveSelfPage) LeaveSelfPage();
}

void LvSettingStaticInfoPage3::create_ui(lv_obj_t *parent)
{
    if (!parent) return;

    ComponensObj = lv_obj_create(parent);
    if (!ComponensObj) return;

    lv_obj_set_size(ComponensObj,
                    metric(LayoutMetric::ScreenW),
                    metric(LayoutMetric::ScreenH));
    lv_obj_set_pos(ComponensObj, 0, 0);
    lv_obj_set_style_bg_color(ComponensObj, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ComponensObj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(ComponensObj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(ComponensObj, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(ComponensObj, 0, LV_PART_MAIN);
    lv_obj_remove_flag(ComponensObj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(ComponensObj, LV_OBJ_FLAG_SCROLLABLE);

    add_label(content_.title,
              metric(LayoutMetric::ContentX),
              metric(LayoutMetric::TitleY),
              0x58A6FF,
              settings_fonts::sans(16, LV_FREETYPE_FONT_STYLE_BOLD),
              false);

    int y = metric(LayoutMetric::LinesY);
    for (const std::string &line : content_.lines) {
        if (y >= metric(LayoutMetric::LinesBottomY)) break;
        lv_obj_t *label = add_label(line,
                                    metric(LayoutMetric::ContentX),
                                    y,
                                    0xE0E0E0,
                                    settings_fonts::cjk_sans(12),
                                    true);
        if (!label) break;
        lv_obj_update_layout(label);
        y += lv_obj_get_height(label) + metric(LayoutMetric::LineGap);
    }

    const lv_font_t *hint_font = settings_fonts::sans(12, LV_FREETYPE_FONT_STYLE_BOLD);
    add_label("ESC: back",
              metric(LayoutMetric::ContentX),
              metric(LayoutMetric::FooterY),
              0x46DC87,
              hint_font ? hint_font : settings_fonts::sans(12),
              false);
    DComponens::lvgl_bind_event(
        ComponensObj, LV_EVENT_KEY, nullptr,
        [this](lv_event_t *event) { handle_key_event(event); });
}

lv_obj_t *LvSettingStaticInfoPage3::add_label(const std::string &text,
                                              int x,
                                              int y,
                                              uint32_t color,
                                              const lv_font_t *font,
                                              bool wrap)
{
    lv_obj_t *label = lv_label_create(ComponensObj);
    if (!label) return nullptr;
    lv_label_set_text(label, text.c_str());
    lv_obj_set_pos(label, x, y);
    lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
    if (font) lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    if (wrap) {
        lv_obj_set_width(label, metric(LayoutMetric::ContentW));
        lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    }
    return label;
}

void LvSettingStaticInfoPage3::handle_key_event(lv_event_t *event)
{
    if (!event || lv_event_get_code(event) != LV_EVENT_KEY) return;
    const uint32_t key = lv_event_get_key(event);
    if (key == LV_KEY_ESC || key == LV_KEY_LEFT) {
        if (LeaveSelfPage) LeaveSelfPage();
        lv_event_stop_processing(event);
    }
}

std::unique_ptr<DComponens::LvglComponensBase> settings_t12b_about_page_factory(
    lv_obj_t *parent,
    const NodeIter &page_node,
    std::function<void()> on_back)
{
    return std::make_unique<LvSettingStaticInfoPage3>(
        parent,
        page_node,
        std::move(on_back),
        settings_t12b::about_help::about(
            build_version(), build_date(), build_channel(), build_commit()));
}

std::unique_ptr<DComponens::LvglComponensBase> settings_t12b_help_page_factory(
    lv_obj_t *parent,
    const NodeIter &page_node,
    std::function<void()> on_back)
{
    return std::make_unique<LvSettingStaticInfoPage3>(
        parent, page_node, std::move(on_back), settings_t12b::about_help::help());
}
