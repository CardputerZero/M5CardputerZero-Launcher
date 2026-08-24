#include "../main/ui/settings_screen_api.hpp"

#include <cassert>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace {

struct FakeScreenBackend {
    int backlight_maximum = 200;
    int backlight_value = 200;
    int brightness_config = 200;
    int dark_time = 30;
    int save_failures = 0;
    int settings_write_failures = 0;
    int config_set_failures = 0;
    bool invalid_backlight_read = false;
    int invalid_backlight_writes = 0;
    bool invalid_config_get = false;
    std::vector<settings_screen::Arguments> settings_calls;
    std::vector<settings_screen::Arguments> config_calls;

    settings_screen::SettingsInvoker settings_invoker()
    {
        return [this](settings_screen::Arguments arguments, settings_screen::Callback callback) {
            settings_calls.push_back(arguments);
            const std::string command = arguments.front();
            if (command == "BacklightMax") {
                callback(0, std::to_string(backlight_maximum));
                return;
            }
            if (command == "BacklightRead") {
                callback(0, invalid_backlight_read ? "not-a-number" : std::to_string(backlight_value));
                return;
            }
            if (command == "BacklightWrite") {
                if (settings_write_failures > 0) {
                    --settings_write_failures;
                    callback(-1, "write failed");
                    return;
                }
                if (invalid_backlight_writes > 0) {
                    --invalid_backlight_writes;
                    callback(0, "not-a-number");
                    return;
                }
                backlight_value = std::stoi(*std::next(arguments.begin()));
                callback(0, std::to_string(backlight_value));
                return;
            }
            callback(-1, "unknown settings command");
        };
    }

    settings_screen::ConfigInvoker config_invoker()
    {
        return [this](settings_screen::Arguments arguments, settings_screen::Callback callback) {
            config_calls.push_back(arguments);
            const std::string command = arguments.front();
            if (command == "GetInt") {
                if (invalid_config_get) {
                    callback(0, "12junk");
                    return;
                }
                const std::string &key = *std::next(arguments.begin());
                if (key == settings_screen::kBrightnessKey) {
                    callback(0, std::to_string(brightness_config));
                    return;
                }
                callback(0, std::to_string(dark_time));
                return;
            }
            if (command == "SetInt") {
                if (config_set_failures > 0) {
                    --config_set_failures;
                    callback(-1, "set failed");
                    return;
                }
                const std::string &key = *std::next(arguments.begin());
                const int value = std::stoi(*std::next(arguments.begin(), 2));
                if (key == settings_screen::kBrightnessKey)
                    brightness_config = value;
                else
                    dark_time = value;
                callback(0, "ok");
                return;
            }
            if (command == "Save") {
                if (save_failures > 0) {
                    --save_failures;
                    callback(-1, "save failed");
                    return;
                }
                callback(0, "ok");
                return;
            }
            callback(-1, "unknown config command");
        };
    }
};

void test_brightness_mapping()
{
    assert(settings_screen::brightness_index(200, 200) == 0);
    assert(settings_screen::brightness_index(150, 200) == 1);
    assert(settings_screen::brightness_index(100, 200) == 2);
    assert(settings_screen::brightness_index(50, 200) == 3);
    assert(settings_screen::brightness_index(0, 200) == 4);
    assert(settings_screen::brightness_value(0, 200) == 200);
    assert(settings_screen::brightness_value(1, 200) == 150);
    assert(settings_screen::brightness_value(2, 200) == 100);
    assert(settings_screen::brightness_value(3, 200) == 50);
    assert(settings_screen::brightness_value(4, 200) == 0);

    assert(settings_screen::brightness_index(0, 100, 4) == 3);
    assert(settings_screen::brightness_value(3, 200, 4) == 50);
    assert(settings_screen::brightness_value(0, 200, 4) == 200);
    assert(!settings_screen::brightness_value_valid(-1, 200));
    assert(!settings_screen::brightness_value_valid(201, 200));

    assert(settings_screen::brightness_index(255, 255) == 0);
    assert(settings_screen::brightness_index(191, 255) == 1);
    assert(settings_screen::brightness_index(127, 255) == 2);
    assert(settings_screen::brightness_index(63, 255) == 3);
    assert(settings_screen::brightness_index(0, 255) == 4);
    assert(settings_screen::brightness_value(0, 255) == 255);
    assert(settings_screen::brightness_value(1, 255) == 191);
    assert(settings_screen::brightness_value(2, 255) == 127);
    assert(settings_screen::brightness_value(3, 255) == 63);
    assert(settings_screen::brightness_value(4, 255) == 0);
}

void test_brightness_read_and_write()
{
    FakeScreenBackend backend;
    const auto settings = backend.settings_invoker();
    const auto config = backend.config_invoker();

    const auto read = settings_screen::read_brightness(settings, config, 5);
    assert(read.usable());
    assert(read.status == settings_screen::BrightnessReadStatus::Ok);
    assert(read.maximum == 200);
    assert(read.value == 200);
    assert(read.index == 0);

    const auto write = settings_screen::write_brightness(settings, config, 2, 200, 200, 5);
    assert(write.succeeded());
    assert(write.applied_value == 100);
    assert(backend.backlight_value == 100);
    assert(backend.brightness_config == 100);

    backend.save_failures = 1;
    const auto failed = settings_screen::write_brightness(settings, config, 1, 200, 100, 5);
    assert(!failed.succeeded());
    assert(failed.rollback_attempted);
    assert(failed.rollback_succeeded);
    assert(backend.backlight_value == 100);
    assert(backend.brightness_config == 100);

    backend.invalid_backlight_read = true;
    const auto fallback = settings_screen::read_brightness(settings, config, 5);
    assert(fallback.usable());
    assert(fallback.defaulted());
    assert(fallback.value == backend.brightness_config);

    backend.invalid_config_get = true;
    const auto config_failure = settings_screen::write_brightness(settings, config, 1, 200, 100, 5);
    assert(!config_failure.succeeded());
    assert(config_failure.status == settings_screen::BrightnessWriteStatus::ConfigReadFailed);

    backend.invalid_config_get = false;
    backend.config_set_failures = 1;
    const auto config_write_failure = settings_screen::write_brightness(settings, config, 1, 200, 100, 5);
    assert(!config_write_failure.succeeded());
    assert(config_write_failure.status == settings_screen::BrightnessWriteStatus::ConfigWriteFailed);
    assert(config_write_failure.rollback_attempted);
    assert(config_write_failure.rollback_succeeded);
    assert(backend.backlight_value == 100);
    assert(backend.brightness_config == 100);

    backend.invalid_backlight_writes = 1;
    const auto payload_failure = settings_screen::write_brightness(settings, config, 1, 200, 100, 5);
    assert(!payload_failure.succeeded());
    assert(payload_failure.status == settings_screen::BrightnessWriteStatus::BacklightPayloadInvalid);
    assert(payload_failure.rollback_attempted);
    assert(payload_failure.rollback_succeeded);
    assert(backend.backlight_value == 100);
    assert(backend.brightness_config == 100);
}

void test_dark_time_read_and_write()
{
    FakeScreenBackend backend;
    const auto config = backend.config_invoker();

    for (int index = 0; index < 5; ++index) {
        backend.dark_time = settings_screen::dark_time_seconds(index);
        const auto read = settings_screen::read_dark_time(config);
        assert(read.usable());
        assert(read.index == index);
        assert(read.seconds == backend.dark_time);
    }

    for (int index = 0; index < 5; ++index) {
        const auto write = settings_screen::write_dark_time(config, index);
        assert(write.succeeded());
        assert(write.applied_seconds == settings_screen::dark_time_seconds(index));
        assert(backend.dark_time == settings_screen::dark_time_seconds(index));
    }

    backend.dark_time = 300;
    backend.save_failures = 1;
    const auto failed = settings_screen::write_dark_time(config, 0);
    assert(!failed.succeeded());
    assert(failed.rollback_attempted);
    assert(failed.rollback_succeeded);
    assert(backend.dark_time == 300);

    backend.invalid_config_get = true;
    const auto defaulted = settings_screen::read_dark_time(config);
    assert(defaulted.usable());
    assert(defaulted.defaulted());
    assert(defaulted.index == 2);

    backend.invalid_config_get = false;
    backend.config_set_failures = 1;
    const auto set_failure = settings_screen::write_dark_time(config, 0);
    assert(!set_failure.succeeded());
    assert(set_failure.status == settings_screen::DarkTimeWriteStatus::SetFailed);
    assert(set_failure.rollback_attempted);
    assert(set_failure.rollback_succeeded);
    assert(backend.dark_time == 300);
}

}

int main()
{
    test_brightness_mapping();
    test_brightness_read_and_write();
    test_dark_time_read_and_write();
    return 0;
}
