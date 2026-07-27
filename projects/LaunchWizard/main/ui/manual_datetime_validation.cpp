#include "manual_datetime_validation.h"

#include <cctype>
#include <cstddef>

namespace launch_wizard {
namespace {

bool parse_digits(const std::string &value, std::size_t start, std::size_t count,
                  int &result)
{
    result = 0;
    for (std::size_t i = start; i < start + count; ++i) {
        if (i >= value.size() || !std::isdigit(static_cast<unsigned char>(value[i])))
            return false;
        result = result * 10 + (value[i] - '0');
    }
    return true;
}

}  // namespace

bool validate_manual_datetime(const std::string &date, const std::string &time,
                              std::string &error)
{
    int year = 0;
    int month = 0;
    int day = 0;
    if (date.size() != 10 || date[4] != '-' || date[7] != '-' ||
        !parse_digits(date, 0, 4, year) || !parse_digits(date, 5, 2, month) ||
        !parse_digits(date, 8, 2, day)) {
        error = "Use date format YYYY-MM-DD.";
        return false;
    }
    if (year < 1 || month < 1 || month > 12) {
        error = "Enter a valid calendar date.";
        return false;
    }

    static constexpr int kDaysPerMonth[] = {31, 28, 31, 30, 31, 30,
                                            31, 31, 30, 31, 30, 31};
    int days_in_month = kDaysPerMonth[month - 1];
    const bool leap_year = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    if (month == 2 && leap_year)
        ++days_in_month;
    if (day < 1 || day > days_in_month) {
        error = "Enter a valid calendar date.";
        return false;
    }

    int hour = 0;
    int minute = 0;
    if (time.size() != 5 || time[2] != ':' || !parse_digits(time, 0, 2, hour) ||
        !parse_digits(time, 3, 2, minute)) {
        error = "Use time format HH:MM.";
        return false;
    }
    if (hour > 23 || minute > 59) {
        error = "Enter a time from 00:00 to 23:59.";
        return false;
    }

    error.clear();
    return true;
}

}  // namespace launch_wizard
