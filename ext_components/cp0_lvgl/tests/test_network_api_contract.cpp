#include "cp0_network_api_contract.hpp"

#include <cassert>
#include <cstring>
#include <list>
#include <string>

namespace {

using cp0::network::ApiCommand;
using cp0::network::ApiRequest;

void test_request_contract()
{
    ApiRequest request;
    assert(!cp0::network::parse_api_request({}, request));
    assert(!cp0::network::parse_api_request({"Unknown"}, request));
    assert(!cp0::network::parse_api_request({"Status", "junk"}, request));
    assert(cp0::network::parse_api_request({"Status"}, request));
    assert(request.command == ApiCommand::Status);

    assert(cp0::network::parse_api_request({"Scan"}, request));
    assert(request.scan_limit == CP0_WIFI_AP_MAX);
    assert(cp0::network::parse_api_request({"Scan", "0"}, request));
    assert(request.scan_limit == 0);
    assert(cp0::network::parse_api_request({"Scan", "32"}, request));
    assert(request.scan_limit == CP0_WIFI_AP_MAX);
    assert(!cp0::network::parse_api_request({"Scan", ""}, request));
    assert(!cp0::network::parse_api_request({"Scan", "12junk"}, request));
    assert(!cp0::network::parse_api_request({"Scan", "-1"}, request));
    assert(!cp0::network::parse_api_request({"Scan", "33"}, request));
    assert(!cp0::network::parse_api_request({"Scan", "999999999999999999999"}, request));
    assert(!cp0::network::parse_api_request({"Scan", "1", "junk"}, request));

    assert(!cp0::network::parse_api_request({"Connect"}, request));
    assert(!cp0::network::parse_api_request({"Connect", ""}, request));
    assert(cp0::network::parse_api_request({"Connect", "office"}, request));
    assert(request.command == ApiCommand::Connect && request.ssid == "office" && request.password.empty());
    assert(cp0::network::parse_api_request({"Connect", "office", "secret"}, request));
    assert(request.password == "secret");
    assert(!cp0::network::parse_api_request({"Connect", "office", "secret", "junk"}, request));
    assert(cp0::network::parse_api_request({"ConnectHidden", "hidden", "secret"}, request));
    assert(request.command == ApiCommand::ConnectHidden && request.ssid == "hidden" &&
           request.password == "secret");
    assert(!cp0::network::parse_api_request({"ConnectHidden", ""}, request));

    assert(!cp0::network::parse_api_request({"ProfileForget", ""}, request));
    assert(!cp0::network::parse_api_request({"ProfileExists"}, request));
    assert(cp0::network::parse_api_request({"ProfileForget", "office"}, request));
    assert(request.command == ApiCommand::ProfileForget && request.ssid == "office");
    assert(cp0::network::parse_api_request({"ProfileExists", "office"}, request));
    assert(request.command == ApiCommand::ProfileExists);

    assert(cp0::network::parse_api_request({"RadioSetEnabled", "true"}, request));
    assert(request.radio_enabled);
    assert(cp0::network::parse_api_request({"RadioSetEnabled", "off"}, request));
    assert(!request.radio_enabled);
    assert(!cp0::network::parse_api_request({"RadioSetEnabled", "yes"}, request));
    assert(!cp0::network::parse_api_request({"RadioEnabled", "junk"}, request));
    assert(std::string(cp0::network::invalid_api_request_message()) == "invalid wifi api request");
}

void test_status_payload_round_trip_and_validation()
{
    cp0_wifi_status_t source{};
    source.connected = 1;
    std::strcpy(source.ssid, "office:east\\floor\n2");
    std::strcpy(source.ip, "192.168.1.8");
    source.signal = 87;
    source.ethernet = 1;

    const std::string payload = cp0::network::encode_status_payload(source);
    cp0_wifi_status_t decoded{};
    assert(cp0::network::decode_status_payload(payload, decoded));
    assert(decoded.connected == 1 && decoded.signal == 87 && decoded.ethernet == 1);
    assert(std::string(decoded.ssid) == source.ssid);
    assert(std::string(decoded.ip) == source.ip);

    assert(!cp0::network::decode_status_payload("1:ssid:ip:50", decoded));
    assert(!cp0::network::decode_status_payload("1:ssid:ip:50:0:junk", decoded));
    assert(!cp0::network::decode_status_payload("2:ssid:ip:50:0", decoded));
    assert(!cp0::network::decode_status_payload("1:ssid:ip:101:0", decoded));
    assert(!cp0::network::decode_status_payload("1:ssid:ip:5x:0", decoded));
    assert(!cp0::network::decode_status_payload("1:bad\\q:ip:50:0", decoded));
}

void test_scan_payload_round_trip_and_validation()
{
    cp0_wifi_ap_t source[2]{};
    std::strcpy(source[0].ssid, "office:east");
    source[0].signal = 76;
    std::strcpy(source[0].security, "WPA2\\WPA3");
    source[0].in_use = 1;
    source[0].saved = 1;
    std::strcpy(source[1].ssid, "guest\nlevel2");
    source[1].signal = 40;
    std::strcpy(source[1].security, "OPEN");

    const std::string payload = cp0::network::encode_scan_payload(source, 2);
    cp0_wifi_ap_t decoded[4]{};
    assert(cp0::network::decode_scan_payload(payload, decoded, 4) == 2);
    assert(std::string(decoded[0].ssid) == source[0].ssid);
    assert(std::string(decoded[0].security) == source[0].security);
    assert(decoded[0].signal == 76 && decoded[0].in_use == 1 && decoded[0].saved == 1);
    assert(std::string(decoded[1].ssid) == source[1].ssid);

    const std::string malformed =
        "valid:50:OPEN:0\n"
        "junk:5x:OPEN:0\n"
        "high:101:OPEN:0\n"
        ":20:OPEN:0\n"
        "extra:20:OPEN:0:junk\n"
        "also-valid:0:OPEN:1\n";
    assert(cp0::network::decode_scan_payload(malformed, decoded, 4) == 2);
    assert(std::string(decoded[0].ssid) == "valid");
    assert(std::string(decoded[1].ssid) == "also-valid");
    assert(cp0::network::decode_scan_payload(payload, nullptr, 4) == 0);
    assert(cp0::network::decode_scan_payload(payload, decoded, 0) == 0);
    assert(cp0::network::encode_scan_payload(nullptr, 2).empty());
}

} // namespace

int main()
{
    test_request_contract();
    test_status_payload_round_trip_and_validation();
    test_scan_payload_round_trip_and_validation();
}
