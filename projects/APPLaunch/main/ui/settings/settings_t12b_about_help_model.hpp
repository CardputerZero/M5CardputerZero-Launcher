#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace settings_t12b::about_help {

struct Content
{
    std::string title;
    std::vector<std::string> lines;
};

Content about(std::string_view version,
              std::string_view build_date,
              std::string_view channel,
              std::string_view commit);
Content help();

} // namespace settings_t12b::about_help
