#include "manual_datetime_validation.h"

#include <iostream>
#include <string>

bool test_wizard_model();

int main()
{
    std::string error;
    const auto expect = [&error](bool expected, const std::string &date,
                                 const std::string &time) {
        const bool actual = launch_wizard::validate_manual_datetime(date, time, error);
        if (actual == expected)
            return true;
        std::cerr << "validation mismatch for " << date << ' ' << time
                  << ": expected " << expected << ", got " << actual
                  << " (" << error << ")\n";
        return false;
    };

    bool passed = true;
    passed &= expect(true, "2026-07-24", "00:00");
    passed &= expect(true, "2026-06-01", "20:30");
    passed &= expect(true, "2024-02-29", "23:59");
    passed &= expect(true, "2000-02-29", "12:00");
    passed &= expect(false, "2023-02-29", "12:00");
    passed &= expect(false, "1900-02-29", "12:00");
    passed &= expect(false, "2026-04-31", "12:00");
    passed &= expect(false, "2026/07/24", "12:00");
    passed &= expect(false, "2026-07-24", "24:00");
    passed &= expect(false, "2026-07-24", "12:60");
    passed &= expect(false, "2026-07-24", "9:30");
    passed &= expect(false, "0000-01-01", "00:00");

    passed &= test_wizard_model();
    return passed ? 0 : 1;
}
