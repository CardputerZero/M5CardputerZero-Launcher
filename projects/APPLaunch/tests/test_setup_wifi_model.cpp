#include "../main/ui/model/setup_wifi_model.hpp"

#include <algorithm>
#include <cassert>
#include <string>

int main()
{
    SetupWifiListViewModel list;
    static_assert(SetupWifiListViewModel::KEY_REPEAT_INTERVAL_MS < 100);
    list.begin_scan();
    assert(list.scanning());
    auto scanning = list.snapshot();
    assert(scanning.rows.empty());
    assert(scanning.empty_message == "Scanning for WiFi networks...");
    list.fail_scan("Network service unavailable; retry");
    assert(!list.scanning());
    assert(list.snapshot().empty_message == "Network service unavailable; retry");
    list.begin_scan();
    list.clear_scan_error();

    std::vector<SetupWifiAccessPoint> networks;
    for (int index = 0; index < 7; ++index) {
        networks.push_back({"wifi-" + std::to_string(index), "WPA2", 90 - index,
                            index == 0, index == 3});
    }
    list.apply_scan(networks);
    assert(!list.scanning());
    assert(list.size() == 7);
    assert(list.selected() && list.selected()->ssid == "wifi-0");
    assert(!list.move_selection(-1));
    assert(list.move_selection(1));
    assert(list.move_selection(1));
    assert(list.move_selection(1));
    list.set_status({true, "wifi-0", "192.168.1.2"});
    auto selected = list.snapshot();
    assert(selected.title == "Connected WiFi: wifi-0  192.168.1.2");
    assert(selected.rows.size() == 5);
    assert(selected.rows[2].selected);
    assert(selected.rows[2].ssid == "wifi-3 *");

    list.begin_scan();
    std::vector<SetupWifiAccessPoint> reordered = networks;
    std::rotate(reordered.begin(), reordered.begin() + 3, reordered.end());
    list.apply_scan(std::move(reordered));
    assert(list.selected() && list.selected()->ssid == "wifi-3");
    list.fail_scan("Network service unavailable; retry");
    assert(list.size() == 0);
    assert(list.snapshot().empty_message == "Network service unavailable; retry");
    list.clear_scan_error();
    list.begin_scan();
    list.cancel_scan();
    assert(!list.scanning());
    list.apply_scan({});
    assert(!list.selected());
    assert(list.selected_index() == 0);

    int timer = 0;
    int stale_timer = 0;
    static_assert(noexcept(setup_wifi_feedback_timer_callback_ready(
        static_cast<int *>(nullptr), static_cast<int *>(nullptr),
        SetupWifiFeedbackModel::Token{})));
    assert(setup_wifi_feedback_timer_callback_ready(
        &timer, &timer, SetupWifiFeedbackModel::Token{1}));
    assert(!setup_wifi_feedback_timer_callback_ready(
        &stale_timer, &timer, SetupWifiFeedbackModel::Token{1}));
    assert(!setup_wifi_feedback_timer_callback_ready(
        static_cast<int *>(nullptr), &timer, SetupWifiFeedbackModel::Token{1}));
    assert(!setup_wifi_feedback_timer_callback_ready(
        &timer, &timer, SetupWifiFeedbackModel::Token{}));

    int feedback_screen = 0;
    int stale_screen = 0;
    static_assert(noexcept(setup_wifi_feedback_screen_delete_allowed(
        &feedback_screen, &feedback_screen, &feedback_screen)));
    assert(setup_wifi_feedback_screen_delete_allowed(
        &feedback_screen, &feedback_screen, &feedback_screen));
    assert(!setup_wifi_feedback_screen_delete_allowed(
        &feedback_screen, &stale_screen, &feedback_screen));
    assert(!setup_wifi_feedback_screen_delete_allowed(
        &stale_screen, &stale_screen, &feedback_screen));
    assert(!setup_wifi_feedback_screen_delete_allowed(
        static_cast<int *>(nullptr), &feedback_screen, &feedback_screen));

    SetupWifiPasswordModel model;
    assert(!model.can_submit());
    assert(model.masked_display() == "_");

    model.begin("Office WiFi");
    assert(!model.can_submit());
    assert(model.append("secret"));
    assert(model.can_submit());
    assert(model.password() == "secret");
    assert(model.masked_display() == "******_");
    assert(model.masked_display().find("secret") == std::string::npos);
    assert(!model.append("\n"));
    assert(!model.append("\x80"));
    assert(!model.append("\xc3"));
    assert(!model.append("\xc0\x80"));
    assert(!model.append("\xed\xa0\x80"));

    assert(model.append("\xc3\xa9"));
    assert(model.masked_display() == "*******_");
    assert(model.erase_last());
    assert(model.password() == "secret");

    model.clear_password();
    assert(model.append(std::string(62, 'x')));
    assert(!model.append("\xe2\x82\xac"));
    assert(model.password().size() == 62);
    model.clear_password();
    assert(model.append(std::string(SetupWifiPasswordModel::MAX_PASSWORD_BYTES, 'x')));
    assert(!model.append("y"));
    model.reset();
    assert(model.ssid().empty());
    assert(model.password().empty());
    assert(!model.can_submit());

    auto valid_wpa = [](const std::string &password) {
        SetupWifiPasswordModel candidate;
        candidate.begin("Secure WiFi", "WPA2");
        assert(candidate.append(password));
        return candidate.can_submit();
    };
    assert(!valid_wpa(std::string(7, 'x')));
    assert(valid_wpa(std::string(8, 'x')));
    assert(valid_wpa(std::string(63, 'x')));
    assert(valid_wpa(std::string(64, 'a')));
    assert(!valid_wpa(std::string(64, 'z')));
    SetupWifiPasswordModel enterprise;
    enterprise.begin("Corp", "WPA2 802.1X");
    assert(enterprise.append("valid-looking-password"));
    assert(!enterprise.can_submit());

    SetupWifiSsidModel ssid;
    assert(!ssid.can_submit());
    assert(ssid.append("Hidden WiFi"));
    assert(ssid.can_submit() && ssid.ssid() == "Hidden WiFi");
    assert(ssid.erase_last() && ssid.ssid() == "Hidden WiF");
    ssid.reset();
    assert(!ssid.append(std::string(33, 'x')));
    assert(!ssid.append("\n"));

    SetupWifiFeedbackModel feedback;
    assert(!feedback.pending());
    const auto first_feedback = feedback.begin();
    assert(first_feedback);
    assert(feedback.pending());
    assert(!feedback.begin());
    assert(feedback.complete(first_feedback));
    assert(!feedback.pending());
    assert(!feedback.complete(first_feedback));
    const auto second_feedback = feedback.begin();
    assert(second_feedback && second_feedback != first_feedback);
    assert(!feedback.complete(first_feedback));
    assert(feedback.pending());
    assert(feedback.cancel(second_feedback));
    assert(!feedback.pending());
}
