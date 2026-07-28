#include "cp0_config_json.h"
#include "cp0_config_service.hpp"

#include <cassert>
#include <climits>
#include <stdexcept>
#include <string>

namespace {

std::pair<int, std::string> call(cp0cfg::ConfigService &service,
                                 cp0cfg::ConfigService::Arguments arguments)
{
    std::pair<int, std::string> result{-99, "missing callback"};
    service.api_call(std::move(arguments), [&](int code, std::string data) {
        result = {code, std::move(data)};
    });
    return result;
}

} // namespace

int main()
{
    assert(cp0cfg::parse_legacy_integer("", 7) == 7);
    assert(cp0cfg::parse_legacy_integer("garbage", 7) == 0);
    assert(cp0cfg::parse_legacy_integer("12tail", 7) == 12);
    assert(cp0cfg::parse_legacy_integer(" \t+12tail", 7) == 12);
    assert(cp0cfg::parse_legacy_integer("999999999999999999999", 0) == INT_MAX);
    assert(cp0cfg::parse_legacy_integer("-999999999999999999999", 0) == INT_MIN);

    cp0cfg::Entries persisted{{"count", "4"}, {"empty", ""}};
    cp0cfg::Entries saved;
    int save_result = 0;
    cp0cfg::ConfigService service(
        [&](cp0cfg::Entries &entries) {
            entries = persisted;
            return true;
        },
        [&](const cp0cfg::Entries &entries) {
            saved = entries;
            return save_result;
        });

    assert(call(service, {"GetInt", "count", "9"}) == std::make_pair(0, std::string("4")));
    assert(call(service, {"GetInt", "missing", "9"}) == std::make_pair(0, std::string("9")));
    assert(call(service, {"GetInt", "empty", "9"}) == std::make_pair(0, std::string("0")));
    assert(call(service, {"SetInt", "count", "17junk"}).first == 0);
    assert(call(service, {"SetStr", "label", "hello"}).first == 0);
    assert(call(service, {"GetStr", "label", "fallback"}).second == "hello");
    bool reentered = false;
    service.api_call({"GetStr", "label", "fallback"}, [&](int code, std::string) {
        assert(code == 0);
        reentered = call(service, {"GetInt", "count", "0"}).second == "17";
    });
    assert(reentered);
    assert(call(service, {"Save"}) == std::make_pair(0, std::string("ok")));
    assert(saved.size() == 3);
    assert(saved[0] == std::make_pair(std::string("count"), std::string("17")));

    save_result = -1;
    assert(call(service, {"Save"}) == std::make_pair(-1, std::string("save failed")));
    assert(call(service, {"SetManyAndSave", "ssh_host", "new", "ssh_port", "2222"}) ==
           std::make_pair(-1, std::string("save failed")));
    assert(call(service, {"GetStr", "ssh_host", "old"}).second == "old");
    save_result = 0;
    assert(call(service, {"SetManyAndSave", "ssh_host", "new", "ssh_port", "2222"}) ==
           std::make_pair(0, std::string("ok")));
    assert(call(service, {"GetStr", "ssh_host", "old"}).second == "new");
    assert(call(service, {"GetStr", "ssh_port", "22"}).second == "2222");
    assert(call(service, {"SetManyAndSave", "missing-value"}).first == -1);
    assert(call(service, {"SetManyAndSave", "", "bad"}).first == -1);
    assert(call(service, {}).first == -1);
    assert(call(service, {"Unknown"}).first == -1);

    persisted = {{"reloaded", "yes"}};
    assert(call(service, {"Init"}).first == 0);
    assert(call(service, {"Init"}).first == 0);
    assert(call(service, {"GetStr", "reloaded", "no"}).second == "yes");
    assert(call(service, {"GetStr", "label", "gone"}).second == "gone");

    bool load_ok = false;
    persisted = {{"must", "survive"}};
    assert(call(service, {"Init"}).first == 0);
    cp0cfg::ConfigService *transactional_ptr = nullptr;
    cp0cfg::ConfigService transactional(
        [&](cp0cfg::Entries &entries) {
            if (!load_ok) return false;
            assert(transactional_ptr);
            assert(call(*transactional_ptr, {"GetStr", "retained", "no"}).second == "yes");
            entries = {{"replacement", "yes"}};
            return true;
        },
        [&](const cp0cfg::Entries &) {
            assert(transactional_ptr);
            assert(call(*transactional_ptr, {"GetStr", "replacement", "missing"}).second == "yes");
            return 0;
        });
    transactional_ptr = &transactional;
    assert(call(transactional, {"SetStr", "retained", "yes"}).first == 0);
    assert(call(transactional, {"Init"}) ==
           std::make_pair(-1, std::string("load failed")));
    assert(call(transactional, {"GetStr", "retained", "no"}).second == "yes");
    load_ok = true;
    assert(call(transactional, {"Init"}).first == 0);
    assert(call(transactional, {"GetStr", "replacement", "no"}).second == "yes");
    assert(call(transactional, {"Save"}).first == 0);

    cp0cfg::ConfigService throwing(
        [](cp0cfg::Entries &) -> bool { throw std::runtime_error("load"); },
        [](const cp0cfg::Entries &) -> int { throw std::runtime_error("save"); });
    assert(call(throwing, {"Init"}).first == -1);
    assert(call(throwing, {"Save"}).first == -1);
    throwing.api_call({"GetStr", "missing", "ok"},
                      [](int, std::string) { throw std::runtime_error("callback"); });
    save_result = 0;
    assert(call(service, {"Save"}).first == 0);
    assert(saved == persisted);

    const std::string oversized_value(300, 'v');
    assert(call(service, {"SetStr", "bounded", oversized_value}).first == 0);
    assert(call(service, {"GetStr", "bounded", ""}).second == std::string(255, 'v'));

    cp0cfg::Entries parsed{{"unchanged", "value"}};
    assert(!cp0cfg::from_json("{\"valid\": 1} trailing", parsed));
    assert(parsed == cp0cfg::Entries({{"unchanged", "value"}}));
    assert(!cp0cfg::from_json("{\"valid\": 1, \"broken\":", parsed));
    assert(parsed == cp0cfg::Entries({{"unchanged", "value"}}));
    assert(cp0cfg::from_json("{\"nested\": {\"value\": \"ok\"}}\n", parsed));
    assert(parsed == cp0cfg::Entries({{"nested.value", "ok"}}));

    const cp0cfg::Entries unchanged{{"unchanged", "value"}};
    const char *invalid_json[] = {
        "{\"n\": 01}",
        "{\"n\": 12tail}",
        "{\"n\": 9223372036854775808}",
        "{\"n\": -9223372036854775809}",
        "{\"n\": 1.}",
        "{\"n\": 1e}",
        "{\"s\": \"bad\\q\"}",
        "{\"s\": \"bad\\u12x4\"}",
        "{\"s\": \"bad\\ud800\"}",
        "{\"a\": [true false]}",
        "{\"a\": [1,]}",
    };
    for (const char *json : invalid_json) {
        parsed = unchanged;
        assert(!cp0cfg::from_json(json, parsed));
        assert(parsed == unchanged);
    }

    assert(!cp0cfg::looks_numeric("9223372036854775808"));
    assert(!cp0cfg::looks_numeric("12tail"));
    assert(cp0cfg::looks_numeric("-9223372036854775808"));
    assert(!cp0cfg::looks_numeric("007"));

    const cp0cfg::Entries round_trip{
        {"identity.leading_zero", "007"},
        {"identity.text", std::string("line one\nline two") + '\x01'},
        {"limits.maximum", "9223372036854775807"},
        {"limits.minimum", "-9223372036854775808"},
    };
    const std::string encoded = cp0cfg::to_json(round_trip);
    cp0cfg::Entries decoded;
    assert(cp0cfg::from_json(encoded, decoded));
    assert(decoded == round_trip);
    assert(cp0cfg::to_json(decoded) == encoded);
    assert(encoded.find("\\u0001") != std::string::npos);

    assert(cp0cfg::from_json("{\"unicode\": \"\\u4f60\\u597d \\ud83d\\ude00\"}", decoded));
    assert(decoded == cp0cfg::Entries({{"unicode", "\xe4\xbd\xa0\xe5\xa5\xbd \xf0\x9f\x98\x80"}}));
}
