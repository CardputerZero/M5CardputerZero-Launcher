#define APP_PAGE_IMPLEMENTATION_UNIT
#include "../ui_app_setup.hpp"
#include "setup_page_access.hpp"

#include <cstdio>
#include <memory>
#include <new>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace setting {

void BluetoothUiSession::copy_string(char *destination, size_t destination_size,
                                     const std::string &source)
{
    if (!destination || destination_size == 0)
        return;
    std::snprintf(destination, destination_size, "%s", source.c_str());
}

int BluetoothUiSession::rfkill_blocked()
{
    const char *argv[] = {
        "/usr/sbin/rfkill", "--noheadings", "--output", "TYPE,SOFT", nullptr
    };
    char output[512] = {};
    if (cp0_process_capture_argv(argv, output, sizeof(output)) != 0)
        return -1;

    std::istringstream lines(output);
    std::string line;
    while (std::getline(lines, line)) {
        std::istringstream fields(line);
        std::string type;
        std::string soft;
        fields >> type >> soft;
        if (type == "bluetooth")
            return soft == "blocked" ? 1 : 0;
    }
    return -1;
}

void BluetoothUiSession::post_to_ui(std::weak_ptr<BluetoothUiSession> weak,
                                    std::function<void(std::shared_ptr<BluetoothUiSession>)> fn)
{
    auto *ctx = new (std::nothrow) UiPost{std::move(weak), std::move(fn)};
    if (!ctx)
        return;
    if (lv_async_call(ui_post_dispatch, ctx) != LV_RESULT_OK)
        delete ctx;
}

void BluetoothUiSession::ui_post_dispatch(void *user)
{
    std::unique_ptr<UiPost> ctx(static_cast<UiPost *>(user));
    try {
        if (auto self = ctx->weak.lock())
            ctx->fn(std::move(self));
    } catch (...) {
    }
}

void BluetoothUiSession::bt_api(
    std::list<std::string> args,
    std::function<void(std::shared_ptr<BluetoothUiSession>, int, std::string)> on_result)
{
    auto weak = weak_from_this();
    cp0_signal_bt_api(std::move(args),
        [weak, on_result = std::move(on_result)](int code, std::string data) mutable {
            post_to_ui(weak,
                [on_result = std::move(on_result), code, data = std::move(data)](auto self) mutable {
                    on_result(std::move(self), code, std::move(data));
                });
        });
}

void BluetoothUiSession::timeout_timer_cb(lv_timer_t *timer) noexcept
{
    try {
        auto *self = static_cast<BluetoothUiSession *>(lv_timer_get_user_data(timer));
        if (!self || timer != self->timeout_timer_)
            return;
        self->timeout_timer_ = nullptr;
        std::function<void()> cb = std::move(self->timeout_pending_cb_);
        self->timeout_pending_cb_ = {};
        if (cb)
            cb();
    } catch (...) {
    }
}

BluetoothUiSession::BluetoothUiSession(UISetupPage &page) : page_(&page)
{
    model_.set_named_only(SetupPageAccess(page).config_get_int("bt_named_only", 1) != 0);

    timeout_.set_timer_hooks(
        [this](uint32_t ms, std::function<void()> cb) {
            if (timeout_timer_) {
                lv_timer_delete(timeout_timer_);
                timeout_timer_ = nullptr;
            }
            timeout_pending_cb_ = std::move(cb);
            timeout_timer_ = lv_timer_create(timeout_timer_cb, ms, this);
            if (timeout_timer_)
                lv_timer_set_repeat_count(timeout_timer_, 1);
        },
        [this]() {
            if (timeout_timer_) {
                lv_timer_delete(timeout_timer_);
                timeout_timer_ = nullptr;
            }
            timeout_pending_cb_ = {};
        });

    // The 3-second Bluetooth status-read timeout starts when the UI session
    // object is created, as required by the Bluetooth settings UX. The dialog
    // itself is only shown once the SUB view exists; before that we simply
    // mark the load failed and on_sub_view_created() displays the dialog.
    timeout_.begin(3000, [this]() {
        if (model_.load_state() == BluetoothSessionLoadState::CREATED ||
            model_.load_state() == BluetoothSessionLoadState::LOADING) {
            model_.mark_failed();
            if (page_ && SetupPageAccess(*page_).is_view(SetupViewState::SUB))
                show_timeout_dialog(*page_, "Bluetooth状态读取失败");
        }
    });

    // Establish the backend session synchronously. BtSessionInit returns the
    // session id immediately; the backend starts its own std::async status
    // preload inside BluetoothSessionManager::create(). The UI-side getter is
    // issued after the hidden SUB view has been built; see
    // on_sub_view_created().
    cp0_signal_bt_api({"BtSessionInit"}, [this](int code, std::string data) {
        if (code == 0) {
            session_id_ = data;
            session_ready_ = true;
        } else {
            model_.mark_failed();
        }
    });
}

BluetoothUiSession::~BluetoothUiSession()
{
    model_.mark_stopped();
    clear_timeout();
    if (page_ && sub_view_hidden_)
        reveal_content(*page_);
    action_operation_.shutdown();
    const uint64_t sudo_id = sudo_operation_.shutdown();
    if (sudo_id)
        cp0_signal_sudo_cancel(sudo_id, nullptr);

    if (session_ready_) {
        cp0_signal_bt_api({"BtScanOff", session_id_}, [](int, std::string) {});
        cp0_signal_bt_api({"BtConnectedListDeinit", session_id_}, [](int, std::string) {});
        cp0_signal_bt_api({"BtSessionDeinit", session_id_}, [](int, std::string) {});
    }
}

void BluetoothUiSession::append(UISetupPage &page, std::vector<MenuItem> &menu)
{
    const bool named_only = SetupPageAccess(page).config_get_int("bt_named_only", 1) != 0;
    MenuItem item;
    item.label = "Bluetooth";
    item.sub_items = {
        {"Power", true, false,
         [&page]() { SetupPageAccess(page).ensure_bluetooth_ui()->toggle_power(page); }},
        {"Alias: CardputerZero", false, false,
         [&page]() { SetupPageAccess(page).ensure_bluetooth_ui()->enter_alias(page); }},
        {"Discoverable", true, false,
         [&page]() { SetupPageAccess(page).ensure_bluetooth_ui()->toggle_discoverable(page); }},
        {"Named Only", true, named_only,
         [&page]() { SetupPageAccess(page).ensure_bluetooth_ui()->toggle_named_only(page); }},
        {"Connected", false, false,
         [&page]() { SetupPageAccess(page).ensure_bluetooth_ui()->enter_devices(page); }},
        {"Scan", false, false,
         [&page]() { SetupPageAccess(page).ensure_bluetooth_ui()->enter_scan(page); }},
    };
    item.on_enter = [&page]() { SetupPageAccess(page).ensure_bluetooth_ui(); };
    menu.push_back(std::move(item));
}

void BluetoothUiSession::begin_initial_load(UISetupPage &page)
{
    (void)page;
    // The session constructor already started the 3-second timeout and sent
    // BtSessionInit. The actual BtStatusGet is deliberately deferred until the
    // SUB view has been built; see on_sub_view_created().
}

void BluetoothUiSession::on_sub_view_created(UISetupPage &page)
{
    if (!session_ready_) {
        // BtSessionInit failed synchronously while the view was being built.
        model_.mark_failed();
        clear_timeout();
        show_timeout_dialog(page, "Bluetooth状态读取失败");
        return;
    }

    switch (model_.load_state()) {
    case BluetoothSessionLoadState::CREATED:
        model_.begin_load();
        hide_content(page);
        request_initial_status(page);
        break;
    case BluetoothSessionLoadState::LOADING:
        hide_content(page);
        break;
    case BluetoothSessionLoadState::READY:
        reveal_content(page);
        update_menu_from_status(page);
        break;
    case BluetoothSessionLoadState::FAILED:
        show_timeout_dialog(page, "Bluetooth状态读取失败");
        break;
    case BluetoothSessionLoadState::STOPPED:
        break;
    }
}

void BluetoothUiSession::request_initial_status(UISetupPage &page)
{
    if (!session_ready_ || model_.load_state() != BluetoothSessionLoadState::LOADING)
        return;

    bt_api({"BtStatusGet", session_id_}, [this](auto, int code, std::string data) {
        if (model_.load_state() != BluetoothSessionLoadState::LOADING)
            return;
        BluetoothStatusSnapshot snapshot;
        const bool ok = (code == 0) && BluetoothStatusSnapshot::decode(data, snapshot);
        if (!ok) {
            finish_session_load_failed(*page_);
            return;
        }
        clear_timeout();
        model_.apply_status(snapshot);
        model_.mark_ready();
        update_menu_from_status(*page_);
        reveal_content(*page_);
        if (SetupPageAccess(*page_).is_view(SetupViewState::SUB))
            SetupPageAccess(*page_).build_sub_view();
    });
}

void BluetoothUiSession::finish_session_load_failed(UISetupPage &page)
{
    if (model_.load_state() == BluetoothSessionLoadState::READY ||
        model_.load_state() == BluetoothSessionLoadState::STOPPED)
        return;
    model_.mark_failed();
    clear_timeout();
    if (SetupPageAccess(page).is_view(SetupViewState::SUB))
        show_timeout_dialog(page, "Bluetooth状态读取失败");
}

void BluetoothUiSession::hide_content(UISetupPage &page)
{
    lv_obj_t *container = SetupPageAccess(page).content_container();
    if (!container || sub_view_hidden_)
        return;
    lv_obj_add_flag(container, LV_OBJ_FLAG_HIDDEN);
    sub_view_hidden_ = true;
}

void BluetoothUiSession::reveal_content(UISetupPage &page)
{
    lv_obj_t *container = SetupPageAccess(page).content_container();
    if (container && sub_view_hidden_) {
        lv_obj_clear_flag(container, LV_OBJ_FLAG_HIDDEN);
    }
    sub_view_hidden_ = false;
}

void BluetoothUiSession::refresh_status(UISetupPage &page)
{
    if (!session_ready_)
        return;
    start_timeout(page, "Bluetooth status read failed");
    bt_api({"BtStatusGet", session_id_}, [this](auto, int code, std::string data) {
        BluetoothStatusSnapshot snapshot;
        const bool ok = (code == 0) && BluetoothStatusSnapshot::decode(data, snapshot);
        apply_status_result(ok ? 0 : (code ? code : -1), std::move(snapshot));
    });
}

void BluetoothUiSession::apply_status_result(int code, BluetoothStatusSnapshot snapshot)
{
    clear_timeout();
    if (!page_)
        return;
    if (code != 0) {
        show_action(*page_, "Bluetooth status read failed", 0xFF4444);
        return;
    }
    model_.apply_status(snapshot);
    update_menu_from_status(*page_);
    if (SetupPageAccess(*page_).is_view(SetupViewState::SUB))
        SetupPageAccess(*page_).build_sub_view();
}

void BluetoothUiSession::update_menu_from_status(UISetupPage &page)
{
    for (auto &menu : SetupPageAccess(page).menus()) {
        if (menu.label != "Bluetooth" || menu.sub_items.size() < 3)
            continue;
        menu.sub_items[0].toggle_state = model_.status().powered;
        menu.sub_items[1].label = "Alias: " + model_.alias();
        menu.sub_items[2].toggle_state = model_.discoverable();
        break;
    }
}

void BluetoothUiSession::enter_devices(UISetupPage &page)
{
    if (!model_.status().powered) {
        require_power_enabled(page);
        return;
    }
    leave_scan();
    enter_connected(page);
}

void BluetoothUiSession::enter_connected(UISetupPage &page)
{
    if (model_.load_state() != BluetoothSessionLoadState::READY)
        return;
    model_.set_sub_page(BluetoothSubPage::CONNECTED);
    SetupPageAccess(page).set_view(SetupViewState::BT_LIST);
    connected_active_ = true;
    start_timeout(page, "Connected list read failed");
    bt_api({"BtConnectedListInit", session_id_}, [this](auto, int code, std::string) {
        if (code != 0) {
            clear_timeout();
            reveal_content(*page_);
            show_action(*page_, "Connected list init failed", 0xFF4444);
            return;
        }
        refresh_connected();
    });
    // Build the list now but keep it hidden until the first BtConnectedListGet
    // succeeds, matching the 3-second timeout UX.
    build_list(page);
    hide_content(page);
}

void BluetoothUiSession::refresh_connected()
{
    if (!connected_active_)
        return;
    bt_api({"BtConnectedListGet", session_id_}, [this](auto, int code, std::string data) {
        BluetoothListSnapshot snapshot = BluetoothListSnapshot::decode(data);
        apply_connected_list(code, std::move(snapshot));
    });
}

void BluetoothUiSession::apply_connected_list(int code, BluetoothListSnapshot snapshot)
{
    if (!connected_active_ || !page_)
        return;
    clear_timeout();
    if (code != 0) {
        reveal_content(*page_);
        show_action(*page_, "Connected list read failed", 0xFF4444);
        return;
    }
    model_.apply_list(snapshot);
    reveal_content(*page_);
    if (SetupPageAccess(*page_).is_view(SetupViewState::BT_LIST))
        build_list(*page_);
}

void BluetoothUiSession::leave_connected()
{
    if (!connected_active_)
        return;
    connected_active_ = false;
    clear_timeout(); // a pending connected-list timeout must not fire after ESC
    if (page_)
        reveal_content(*page_);
    if (session_ready_)
        cp0_signal_bt_api({"BtConnectedListDeinit", session_id_}, [](int, std::string) {});
}

void BluetoothUiSession::enter_scan(UISetupPage &page)
{
    if (!model_.status().powered) {
        require_power_enabled(page);
        return;
    }
    leave_connected();
    enter_scan_sub(page);
}

void BluetoothUiSession::enter_scan_sub(UISetupPage &page)
{
    model_.set_sub_page(BluetoothSubPage::SCAN);
    SetupPageAccess(page).set_view(SetupViewState::BT_LIST);
    scan_active_ = true;

    auto weak = weak_from_this();
    const uint64_t generation = scan_generation_;
    cp0_signal_bt_api({"BtScanOn", session_id_},
        [weak, generation](int code, std::string data) {
            // Backend scan thread: deliver each snapshot to the LVGL main
            // thread. The generation token drops snapshots that were posted
            // before a leave_scan() call but dispatched afterwards.
            post_to_ui(weak, [code, data = std::move(data), generation](auto self) {
                self->apply_scan_snapshot(code, std::move(data), generation);
            });
        });
    build_list(page);
}

void BluetoothUiSession::apply_scan_snapshot(int code, std::string data,
                                             uint64_t generation)
{
    if (!scan_active_ || !page_ || generation != scan_generation_)
        return;
    if (code != 0) {
        // Backend could not start/continue the scan thread. Tear the scan
        // state down and show a visible error instead of leaving the list
        // stuck on "Scanning...".
        leave_scan();
        show_action(*page_, "Scan start failed", 0xFF4444);
        return;
    }
    model_.apply_list(BluetoothListSnapshot::decode(data));
    if (SetupPageAccess(*page_).is_view(SetupViewState::BT_LIST))
        build_list(*page_);
}

void BluetoothUiSession::leave_scan()
{
    if (!scan_active_)
        return;
    scan_active_ = false;
    ++scan_generation_;
    clear_timeout();
    if (session_ready_)
        cp0_signal_bt_api({"BtScanOff", session_id_}, [](int, std::string) {});
}

void BluetoothUiSession::enter_alias(UISetupPage &page)
{
    if (!model_.status().powered) {
        require_power_enabled(page);
        return;
    }
    leave_scan();
    leave_connected();
    model_.begin_alias_edit();
    SetupPageAccess(page).set_view(SetupViewState::BT_ALIAS);
    build_alias_view(page);
}

void BluetoothUiSession::handle_alias_key(UISetupPage &page, uint32_t key)
{
    if (key == KEY_ESC || key == KEY_LEFT) {
        alias_input_lbl_ = nullptr;
        alias_hint_lbl_ = nullptr;
        SetupPageAccess access(page);
        access.play_back();
        access.set_view(SetupViewState::SUB);
        access.build_sub_view();
        return;
    }
    if (key == KEY_ENTER || key == KEY_RIGHT) {
        if (!model_.status().powered) {
            require_power_enabled(page);
            return;
        }
        std::string alias = model_.sanitized_alias();
        if (alias_hint_lbl_) {
            lv_label_set_text(alias_hint_lbl_, "Setting alias...");
            lv_obj_set_style_text_color(alias_hint_lbl_, lv_color_hex(0xFFAA00), LV_PART_MAIN);
            lv_refr_now(nullptr);
        }
        bt_api({"BtAlias", session_id_, alias},
               [this, alias](auto, int code, std::string) { apply_alias_result(alias, code); });
        return;
    }
    if (key == KEY_BACKSPACE) {
        model_.erase_alias_character();
        alias_update_display();
        return;
    }
    const std::string_view input = SetupPageAccess(page).current_utf8();
    if (!input.empty()) {
        model_.append_alias_text(input.data());
        alias_update_display();
    }
}

void BluetoothUiSession::apply_alias_result(std::string alias, int code)
{
    if (code == 0) {
        model_.set_alias(alias);
        alias_input_lbl_ = nullptr;
        alias_hint_lbl_ = nullptr;
        update_menu_from_status(*page_);
        SetupPageAccess access(*page_);
        access.set_view(SetupViewState::SUB);
        access.build_sub_view();
    } else if (alias_hint_lbl_) {
        lv_label_set_text(alias_hint_lbl_, "Set failed");
        lv_obj_set_style_text_color(alias_hint_lbl_, lv_color_hex(0xFF4444), LV_PART_MAIN);
    }
}

void BluetoothUiSession::handle_power_warning_key(UISetupPage &page, uint32_t key)
{
    if (key != KEY_ENTER && key != KEY_ESC && key != KEY_LEFT)
        return;
    SetupPageAccess access(page);
    access.set_view(SetupViewState::SUB);
    access.select_sub(0, 6);
    access.build_sub_view();
}

void BluetoothUiSession::require_power_enabled(UISetupPage &page)
{
    if (model_.status().powered)
        return;
    leave_scan();
    leave_connected();
    alias_input_lbl_ = nullptr;
    alias_hint_lbl_ = nullptr;
    SetupPageAccess access(page);
    access.set_view(SetupViewState::BT_POWER_WARNING);
    if (!show_power_warning(page)) {
        access.set_view(SetupViewState::SUB);
        access.select_sub(0, 6);
        access.build_sub_view();
    }
}

void BluetoothUiSession::toggle_power(UISetupPage &page)
{
    bool enabled = false;
    for (auto &menu : SetupPageAccess(page).menus()) {
        if (menu.label != "Bluetooth")
            continue;
        enabled = menu.sub_items[0].toggle_state;
        break;
    }

    if (!enabled) {
        leave_scan();
        set_power_async(0);
        return;
    }

    if (sudo_operation_.active()) {
        refresh_status(page);
        return;
    }

    const int blocked = rfkill_blocked();
    if (blocked <= 0) {
        if (blocked < 0)
            std::fprintf(stderr, "Bluetooth: unable to query /usr/sbin/rfkill; trying BlueZ power on\n");
        set_power_async(1);
        return;
    }

    struct SudoContext {
        std::weak_ptr<BluetoothUiSession> weak;
        AsyncOperationLifecycle::Token token;
    };
    AsyncOperationLifecycle::Token token = sudo_operation_.begin();
    auto *context = new (std::nothrow) SudoContext{weak_from_this(), token};
    if (!context) {
        sudo_operation_.abort(token);
        refresh_status(page);
        return;
    }

    uint64_t request_id = 0;
    int result = -1;
    cp0_signal_sudo_argv_async({"/usr/sbin/rfkill", "unblock", "bluetooth"}, 60000, 30000,
        [context](int result_code, int exit_code) {
            std::unique_ptr<SudoContext> owned(context);
            if (!context->token.complete())
                return;
            post_to_ui(context->weak, [result_code, exit_code](auto self) {
                // rfkill's exit status is not authoritative. It can report a
                // non-zero result when the adapter is already unblocked.
                // Verify the actual state without sudo before continuing.
                const int blocked = rfkill_blocked();
                if (blocked == 0)
                    self->set_power_async(1);
                else {
                    std::fprintf(stderr,
                                 "Bluetooth: rfkill unblock result=%d exit=%d, state=%d\n",
                                 result_code, exit_code, blocked);
                    self->show_action(*self->page_, "Bluetooth power change failed", 0xFF4444);
                    self->refresh_status(*self->page_);
                }
            });
        },
        [&](int code, uint64_t id) {
            result = code;
            request_id = id;
        });
    if (result != 0) {
        delete context;
        sudo_operation_.abort(token);
        refresh_status(page);
    } else {
        sudo_operation_.activate(token, request_id);
    }
}

void BluetoothUiSession::set_power_async(int on)
{
    const uint64_t generation = ++power_generation_;
    // Start the Power timeout only after rfkill/sudo has completed. Password
    // entry time is therefore not included in the 3-second BlueZ deadline.
    const std::string timeout_message = "Bluetooth power change timed out";
    timeout_.begin(3000, [this, generation, timeout_message]() {
        if (generation != power_generation_)
            return;
        ++power_generation_;
        if (page_)
            show_timeout_dialog(*page_, timeout_message);
    });
    bt_api({"BtPower", session_id_, std::to_string(on)},
           [this, generation](auto, int code, std::string) {
               apply_power_result(code, generation);
           });
}

void BluetoothUiSession::apply_power_result(int code, uint64_t generation)
{
    if (generation != power_generation_)
        return;
    ++power_generation_;
    clear_timeout();
    if (code == 0) {
        refresh_status(*page_);
        return;
    }

    // A failed setter result does not necessarily mean that BlueZ rejected
    // the request; the property update can complete after the method reply.
    // Read the authoritative adapter state before showing an error.
    bt_api({"BtStatusGet", session_id_}, [this, generation](auto, int status_code, std::string data) {
        if (generation + 1 != power_generation_)
            return;
        BluetoothStatusSnapshot snapshot;
        const bool powered = status_code == 0 &&
            BluetoothStatusSnapshot::decode(data, snapshot) && snapshot.powered;
        if (powered) {
            model_.apply_status(snapshot);
            update_menu_from_status(*page_);
            if (SetupPageAccess(*page_).is_view(SetupViewState::SUB))
                SetupPageAccess(*page_).build_sub_view();
            return;
        }
        show_action(*page_, "Bluetooth power change failed", 0xFF4444);
        refresh_status(*page_);
    });
}

void BluetoothUiSession::toggle_named_only(UISetupPage &page)
{
    SetupPageAccess access(page);
    for (auto &menu : access.menus()) {
        if (menu.label != "Bluetooth" || menu.sub_items.size() < 4)
            continue;
        const bool previous = access.config_get_int("bt_named_only", 1) != 0;
        const bool desired = menu.sub_items[3].toggle_state;
        if (!access.config_set_int("bt_named_only", desired ? 1 : 0) || !access.config_save()) {
            access.config_set_int("bt_named_only", previous ? 1 : 0);
            access.config_save();
            menu.sub_items[3].toggle_state = previous;
            model_.set_named_only(previous);
        } else {
            model_.set_named_only(desired);
        }
        break;
    }
    if (access.is_view(SetupViewState::BT_LIST))
        build_list(page);
}

void BluetoothUiSession::toggle_discoverable(UISetupPage &page)
{
    if (!model_.status().powered) {
        require_power_enabled(page);
        return;
    }
    bool desired = false;
    for (auto &menu : SetupPageAccess(page).menus()) {
        if (menu.label != "Bluetooth")
            continue;
        desired = menu.sub_items[2].toggle_state;
        break;
    }
    model_.set_discoverable(desired);
    bt_api({"BtDiscoverable", session_id_, std::to_string(desired ? 1 : 0)},
        [this, desired](auto, int code, std::string) {
            if (code != 0) {
                model_.set_discoverable(!desired);
                show_action(*page_, "Discoverable change failed", 0xFF4444);
            }
            update_menu_from_status(*page_);
            if (SetupPageAccess(*page_).is_view(SetupViewState::SUB))
                SetupPageAccess(*page_).build_sub_view();
        });
}

void BluetoothUiSession::activate_selected(UISetupPage &page)
{
    if (!model_.status().powered) {
        require_power_enabled(page);
        return;
    }
    if (action_busy_ && action_operation_.active())
        return;
    const int dev_index = model_.selected_device_index();
    if (dev_index < 0)
        return;
    const BluetoothDeviceState &device = model_.list().devices[dev_index];

    action_busy_ = true;
    action_token_ = action_operation_.begin();

    if (device.connected) {
        show_action(page, "Disconnecting...");
        bt_api({"BtDisconnect", session_id_, device.address},
               [this](auto, int code, std::string) { finish_device_action("disconnect", code); });
    } else if (device.paired) {
        show_action(page, "Connecting...");
        bt_api({"BtConnect", session_id_, device.address},
               [this](auto, int code, std::string) { finish_device_action("connect", code); });
    } else {
        show_action(page, "Pairing...");
        const std::string address = device.address;
        bt_api({"BtPair", session_id_, address},
               [this, address](auto, int code, std::string) {
                   if (code == 0) {
                       bt_api({"BtConnect", session_id_, address},
                              [this](auto, int connect_code, std::string) {
                                  finish_device_action("connect", connect_code);
                              });
                   } else {
                       finish_device_action("pair", code);
                   }
               });
    }
}

void BluetoothUiSession::finish_device_action(const char * /*operation*/, int code)
{
    if (!action_token_.complete())
        return;
    action_busy_ = false;
    if (code != 0)
        show_action(*page_, "Bluetooth action failed", 0xFF4444);

    if (model_.sub_page() == BluetoothSubPage::CONNECTED) {
        refresh_connected();
    } else if (model_.sub_page() == BluetoothSubPage::SCAN) {
        // A successful pair/connect moves the device to the connected list.
        leave_scan();
        model_.set_sub_page(BluetoothSubPage::CONNECTED);
        enter_connected(*page_);
    } else if (SetupPageAccess(*page_).is_view(SetupViewState::BT_LIST)) {
        build_list(*page_);
    }
}

void BluetoothUiSession::remove_selected(UISetupPage &page)
{
    if (!model_.status().powered) {
        require_power_enabled(page);
        return;
    }
    const int dev_index = model_.selected_device_index();
    if (dev_index < 0)
        return;
    const std::string address = model_.list().devices[dev_index].address;
    show_action(page, "Removing...");
    bt_api({"BtRemove", session_id_, address}, [this](auto, int code, std::string) {
        if (code != 0)
            show_action(*page_, "Remove failed", 0xFF4444);
        refresh_connected();
    });
}

void BluetoothUiSession::handle_list_key(UISetupPage &page, uint32_t key)
{
    switch (key) {
    case KEY_UP:
        model_.select_next_device(-1);
        build_list(page);
        break;
    case KEY_DOWN:
        model_.select_next_device(1);
        build_list(page);
        break;
    case KEY_ENTER:
        activate_selected(page);
        break;
    case KEY_D:
        if (model_.sub_page() == BluetoothSubPage::CONNECTED)
            remove_selected(page);
        break;
    case KEY_R:
        if (model_.sub_page() == BluetoothSubPage::SCAN) {
            leave_scan();
            enter_scan_sub(page);
        } else {
            refresh_connected();
        }
        break;
    case KEY_ESC:
    case KEY_LEFT: {
        leave_scan();
        leave_connected();
        action_operation_.abort(action_token_);
        action_busy_ = false;
        SetupPageAccess access(page);
        access.play_back();
        access.set_view(SetupViewState::SUB);
        access.build_sub_view();
        break;
    }
    default:
        break;
    }
}

void BluetoothUiSession::start_timeout(UISetupPage &page, const char *message)
{
    (void)page;
    const std::string text = message ? message : "";
    timeout_.begin(3000, [this, text]() {
        if (page_)
            show_timeout_dialog(*page_, text);
    });
}

void BluetoothUiSession::clear_timeout()
{
    timeout_.cancel();
}

void BluetoothUiSession::show_timeout_dialog(UISetupPage &page, const std::string &message)
{
    reveal_content(page);
    timeout_message_ = message;
    SetupPageAccess(page).set_view(SetupViewState::BT_TIMEOUT);
    build_timeout_view(page);
}

void BluetoothUiSession::handle_timeout_key(UISetupPage &page, uint32_t key)
{
    if (key != KEY_ENTER && key != KEY_ESC && key != KEY_LEFT)
        return;
    leave_scan();
    leave_connected();
    SetupPageAccess access(page);
    access.set_view(SetupViewState::SUB);
    access.select_sub(0, 6);
    access.build_sub_view();
}

} // namespace setting
