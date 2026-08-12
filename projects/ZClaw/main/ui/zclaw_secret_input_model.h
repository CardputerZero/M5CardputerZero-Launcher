#pragma once

#include <string>

namespace zclaw {

inline std::string secret_input_initial_text()
{
    return {};
}

inline std::string apply_secret_input(const std::string &existing,
                                      const std::string &submitted)
{
    return submitted.empty() && !existing.empty() ? existing : submitted;
}

} // namespace zclaw
