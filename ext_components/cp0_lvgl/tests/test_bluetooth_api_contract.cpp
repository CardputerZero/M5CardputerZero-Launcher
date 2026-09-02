#include "cp0_bluetooth_api_contract.hpp"

#include <cassert>
#include <stdexcept>

int main()
{
    cp0::bluetooth::Request request;
    assert(cp0::bluetooth::parse_request({"BtStatus"}, request));
    assert(!cp0::bluetooth::parse_request({"BtStatus", "junk"}, request));
    assert(cp0::bluetooth::parse_request({"BtPower", "0"}, request));
    assert(request.value == 0);
    assert(cp0::bluetooth::parse_request({"BtDiscoverable", "1"}, request));
    assert(request.value == 1);
    assert(!cp0::bluetooth::parse_request({"BtPower"}, request));
    assert(!cp0::bluetooth::parse_request({"BtPower", "junk"}, request));
    assert(!cp0::bluetooth::parse_request({"BtPower", "-1"}, request));
    assert(!cp0::bluetooth::parse_request({"BtPower", "2"}, request));

    assert(cp0::bluetooth::parse_request({"BtScan"}, request));
    assert(request.max_count == 16);
    assert(cp0::bluetooth::parse_request({"BtList", "1"}, request));
    assert(request.max_count == 1);
    assert(cp0::bluetooth::parse_request({"BtConnectedList", "16"}, request));
    assert(!cp0::bluetooth::parse_request({"BtScan", "0"}, request));
    assert(!cp0::bluetooth::parse_request({"BtScan", "-1"}, request));
    assert(!cp0::bluetooth::parse_request({"BtScan", "17"}, request));
    assert(!cp0::bluetooth::parse_request({"BtScan", "999999999999"}, request));
    assert(!cp0::bluetooth::parse_request({"BtScan", "2junk"}, request));

    assert(cp0::bluetooth::parse_request({"BtAlias", "Cardputer"}, request));
    assert(request.text == "Cardputer");
    assert(!cp0::bluetooth::parse_request({"BtAlias"}, request));
    assert(!cp0::bluetooth::parse_request({"BtAlias", ""}, request));
    assert(!cp0::bluetooth::parse_request({"BtAlias", "bad\talias"}, request));
    assert(!cp0::bluetooth::parse_request(
        {"BtAlias", std::string("bad\0alias", 9)}, request));
    assert(!cp0::bluetooth::parse_request({"BtAlias", std::string(64, 'a')}, request));
    assert(cp0::bluetooth::parse_request({"BtAlias", std::string(63, 'a')}, request));

    const char *address = "AA:bb:01:23:45:67";
    assert(cp0::bluetooth::parse_request({"BtPair", address}, request));
    assert(cp0::bluetooth::parse_request({"BtCancelPairing", address}, request));
    assert(cp0::bluetooth::parse_request({"BtConnect", address}, request));
    assert(cp0::bluetooth::parse_request({"BtDisconnect", address}, request));
    assert(cp0::bluetooth::parse_request({"BtRemove", address}, request));
    assert(!cp0::bluetooth::parse_request({"BtPair"}, request));
    assert(!cp0::bluetooth::parse_request({"BtPair", "not-an-address"}, request));
    assert(!cp0::bluetooth::parse_request({"BtPair", address, "junk"}, request));

    assert(cp0::bluetooth::sanitize_wire_field("normal") == "normal");
    assert(cp0::bluetooth::sanitize_wire_field(
               std::string("name\twith\rcontrols\n\0", 20)) ==
           std::string("name with controls  ", 20));

    int calls = 0;
    cp0::bluetooth::invoke_callback([&](int code, std::string data) {
        ++calls;
        assert(code == 3 && data == "ok");
        throw std::runtime_error("callback");
    }, 3, "ok");
    assert(calls == 1);

    calls = 0;
    cp0::bluetooth::invoke_backend([&](int code, std::string data) {
        ++calls;
        assert(code == -1 && data == "bluetooth backend failure");
    }, []() -> cp0::bluetooth::Reply { throw std::runtime_error("backend"); });
    assert(calls == 1);

    calls = 0;
    cp0::bluetooth::invoke_backend([&](int code, std::string data) {
        ++calls;
        assert(code == 7 && data == "done");
    }, [] { return cp0::bluetooth::Reply{7, "done"}; });
    assert(calls == 1);
}
