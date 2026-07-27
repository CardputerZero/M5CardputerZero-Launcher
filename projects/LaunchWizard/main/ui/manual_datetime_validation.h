#ifndef LAUNCH_WIZARD_MANUAL_DATETIME_VALIDATION_H
#define LAUNCH_WIZARD_MANUAL_DATETIME_VALIDATION_H

#include <string>

namespace launch_wizard {

bool validate_manual_datetime(const std::string &date, const std::string &time,
                              std::string &error);

} // namespace launch_wizard

#endif  // LAUNCH_WIZARD_MANUAL_DATETIME_VALIDATION_H
