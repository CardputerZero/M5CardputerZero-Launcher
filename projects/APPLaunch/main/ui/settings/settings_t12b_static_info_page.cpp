#include "settings_t12b_static_info_page.hpp"

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
