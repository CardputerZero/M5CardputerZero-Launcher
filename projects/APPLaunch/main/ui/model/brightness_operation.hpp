#pragma once

#include <mutex>

namespace brightness_control {

inline std::mutex &operation_mutex()
{
    static std::mutex mutex;
    return mutex;
}

} // namespace brightness_control
