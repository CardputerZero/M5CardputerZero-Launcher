#include "settings_camera_resolution_adapter.hpp"

#include <cassert>
#include <climits>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace Adapter = settings_camera_resolution;

class FakeConfig {
public:
    Adapter::ConfigInvoker invoker()
    {
        return [this](Adapter::ConfigArguments arguments, Adapter::ConfigCallback callback) {
            handle(std::move(arguments), std::move(callback));
        };
    }

    std::map<std::string, std::string> values{
        {Adapter::kWidthKey, "1280"},
        {Adapter::kHeightKey, "720"},
    };
    std::vector<std::vector<std::string>> calls;
    std::string fail_height_value;
    bool fail_save_once = false;
    bool return_empty_set_payload = false;

private:
    void handle(Adapter::ConfigArguments arguments, Adapter::ConfigCallback callback)
    {
        std::vector<std::string> recorded(arguments.begin(), arguments.end());
        calls.push_back(recorded);
        if (recorded.empty()) {
            callback(-1, "empty command");
            return;
        }

        if (recorded.front() == "GetInt" && recorded.size() == 3) {
            const auto value = values.find(recorded[1]);
            callback(0, value == values.end() ? recorded[2] : value->second);
            return;
        }

        if (recorded.front() == "SetInt" && recorded.size() == 3) {
            if (recorded[1] == Adapter::kHeightKey && recorded[2] == fail_height_value) {
                fail_height_value.clear();
                callback(-1, "unsupported height");
                return;
            }
            if (return_empty_set_payload) {
                return_empty_set_payload = false;
                callback(0, "");
                return;
            }
            values[recorded[1]] = recorded[2];
            callback(0, "ok");
            return;
        }

        if (recorded.front() == "Save" && recorded.size() == 1) {
            if (fail_save_once) {
                fail_save_once = false;
                callback(-1, "save failed");
                return;
            }
            callback(0, "ok");
            return;
        }

        callback(-1, "unknown command");
    }
};

void test_integer_parser()
{
    int parsed = 0;
    assert(Adapter::parse_integer("42", parsed) && parsed == 42);
    assert(Adapter::parse_integer("  +42", parsed) && parsed == 42);
    assert(Adapter::parse_integer(std::to_string(INT_MIN), parsed) && parsed == INT_MIN);
    assert(Adapter::parse_integer(std::to_string(INT_MAX), parsed) && parsed == INT_MAX);
    assert(!Adapter::parse_integer("42 ", parsed));
    assert(!Adapter::parse_integer("12px", parsed));
    assert(!Adapter::parse_integer("", parsed));
    assert(!Adapter::parse_integer("999999999999999999999", parsed));
}

void test_mapping()
{
    assert(Adapter::index_for_resolution({1280, 720}) == 0);
    assert(Adapter::index_for_resolution({640, 480}) == 1);
    assert(Adapter::index_for_resolution({1920, 1080}) == -1);

    Adapter::Resolution resolution{};
    const Adapter::Resolution large_resolution{1280, 720};
    const Adapter::Resolution small_resolution{640, 480};
    assert(Adapter::resolution_for_index(0, resolution) && resolution == large_resolution);
    assert(Adapter::resolution_for_index(1, resolution) && resolution == small_resolution);
    assert(!Adapter::resolution_for_index(2, resolution));
}

void test_read_and_default()
{
    FakeConfig config;
    config.values[Adapter::kWidthKey] = "640";
    config.values[Adapter::kHeightKey] = "480";
    const auto read_result = Adapter::read_resolution(config.invoker());
    const Adapter::Resolution small_resolution{640, 480};
    assert(read_result.status == Adapter::ReadStatus::Ok);
    assert(read_result.resolution == small_resolution);

    config.values[Adapter::kWidthKey] = "1920";
    config.values[Adapter::kHeightKey] = "1080";
    const auto defaulted_result = Adapter::read_resolution(config.invoker());
    assert(defaulted_result.status == Adapter::ReadStatus::Defaulted);
    assert(defaulted_result.defaulted());
    assert(defaulted_result.resolution == Adapter::kDefaultResolution);

    FakeConfig failed_config;
    failed_config.values.clear();
    const auto missing_result = Adapter::read_resolution(failed_config.invoker());
    assert(missing_result.status == Adapter::ReadStatus::Ok);
    assert(missing_result.resolution == Adapter::kDefaultResolution);

    const Adapter::ConfigInvoker no_backend;
    const auto backend_result = Adapter::read_resolution(no_backend);
    assert(backend_result.status == Adapter::ReadStatus::BackendError);
}

void test_paired_write()
{
    FakeConfig config;
    const auto result = Adapter::write_resolution(config.invoker(), {640, 480});
    assert(result.succeeded());
    assert(config.values[Adapter::kWidthKey] == "640");
    assert(config.values[Adapter::kHeightKey] == "480");
    assert(config.calls.size() == 5);
    assert(config.calls[0][0] == "GetInt");
    assert(config.calls[1][0] == "GetInt");
    const std::vector<std::string> set_width_call{"SetInt", Adapter::kWidthKey, "640"};
    const std::vector<std::string> set_height_call{"SetInt", Adapter::kHeightKey, "480"};
    const std::vector<std::string> save_call{"Save"};
    assert(config.calls[2] == set_width_call);
    assert(config.calls[3] == set_height_call);
    assert(config.calls[4] == save_call);
}

void test_height_failure_rolls_back()
{
    FakeConfig config;
    config.fail_height_value = "480";
    const auto result = Adapter::write_resolution(config.invoker(), {640, 480});
    assert(result.status == Adapter::WriteStatus::HeightWriteFailed);
    assert(result.rollback_attempted);
    assert(result.rollback_succeeded);
    assert(config.values[Adapter::kWidthKey] == "1280");
    assert(config.values[Adapter::kHeightKey] == "720");
}

void test_save_failure_rolls_back()
{
    FakeConfig config;
    config.fail_save_once = true;
    const auto result = Adapter::write_resolution(config.invoker(), {640, 480});
    assert(result.status == Adapter::WriteStatus::SaveFailed);
    assert(result.rollback_attempted);
    assert(result.rollback_succeeded);
    assert(config.values[Adapter::kWidthKey] == "1280");
    assert(config.values[Adapter::kHeightKey] == "720");
}

void test_write_payload_and_target_validation()
{
    FakeConfig invalid_payload_config;
    invalid_payload_config.return_empty_set_payload = true;
    const auto invalid_payload_result =
        Adapter::write_resolution(invalid_payload_config.invoker(), {640, 480});
    assert(invalid_payload_result.status == Adapter::WriteStatus::WidthWriteFailed);
    assert(invalid_payload_result.rollback_attempted);

    FakeConfig unsupported_config;
    const auto unsupported_result = Adapter::write_resolution(unsupported_config.invoker(), {1920, 1080});
    assert(unsupported_result.status == Adapter::WriteStatus::InvalidTarget);
    assert(unsupported_config.calls.empty());
}

} // namespace

int main()
{
    test_integer_parser();
    test_mapping();
    test_read_and_default();
    test_paired_write();
    test_height_failure_rolls_back();
    test_save_failure_rolls_back();
    test_write_payload_and_target_validation();
    return 0;
}
