#pragma once

#include "bluetooth_page_model.hpp"

#include <string>
#include <utility>
#include <vector>

// Bluetooth sub-page currently on screen.
enum class BluetoothSubPage { NONE, CONNECTED, SCAN };

// Load state of the Bluetooth settings session. The 3-second UI timeout is
// measured from BluetoothUiSession construction to a successful initial
// BtStatusGet.
enum class BluetoothSessionLoadState {
    CREATED, // session constructed, BtSessionInit sent (or in flight)
    LOADING, // hidden UI built, BtStatusGet sent
    READY,   // initial status decoded and applied, UI visible
    FAILED,  // init/get failed or 3-second timeout expired
    STOPPED, // session is being torn down
};

// Pure-data status snapshot. Produced by the backend thread from a status wire
// record and consumed by the LVGL main thread.
struct BluetoothStatusSnapshot
{
    bool powered = false;
    std::string address;
    bool discoverable = false;
    std::string alias;
    bool named_only = true;

    // Decodes a tab-separated status wire record. On failure returns false and
    // leaves `out` untouched.
    static bool decode(const std::string &wire, BluetoothStatusSnapshot &out);
};

// Pure-data device list snapshot. Produced by the backend thread and consumed
// by the LVGL main thread (which then rebuilds the list view).
struct BluetoothListSnapshot
{
    std::vector<BluetoothDeviceState> devices;

    static BluetoothListSnapshot decode(const std::string &wire);
    bool empty() const { return devices.empty(); }
};

// UI model for the Bluetooth settings sub-page. Owns the status/list snapshots,
// the selected sub-page, and the list/alias/named_only interaction state
// (delegated to BluetoothPageModel).
class BluetoothUiModel
{
public:
    BluetoothSubPage sub_page() const { return sub_page_; }
    void set_sub_page(BluetoothSubPage sub_page);

    BluetoothSessionLoadState load_state() const { return load_state_; }
    void begin_load() { load_state_ = BluetoothSessionLoadState::LOADING; }
    void mark_ready() { load_state_ = BluetoothSessionLoadState::READY; }
    void mark_failed() { load_state_ = BluetoothSessionLoadState::FAILED; }
    void mark_stopped() { load_state_ = BluetoothSessionLoadState::STOPPED; }
    bool is_initial_load_finished() const
    {
        return load_state_ == BluetoothSessionLoadState::READY ||
               load_state_ == BluetoothSessionLoadState::FAILED ||
               load_state_ == BluetoothSessionLoadState::STOPPED;
    }

    const BluetoothStatusSnapshot &status() const { return status_; }
    void apply_status(const BluetoothStatusSnapshot &status);

    const BluetoothListSnapshot &list() const { return list_; }
    void apply_list(const BluetoothListSnapshot &list);

    BluetoothListMode list_mode() const { return page_.list_mode(); }
    void clear_selection() { page_.clear_selection(); }
    void select_next_device(int direction) { page_.select_next_device(direction); }
    int selected_device_index() const { return page_.selected_device_index(); }
    int list_selection() const { return page_.list_selection(); }
    const std::vector<BluetoothListRow> &rows() const { return page_.rows(); }

    void set_named_only(bool enabled)
    {
        page_.set_named_only(enabled);
        // named_only changes which devices are hidden, so re-derive rows from
        // the current list snapshot.
        page_.rebuild_rows(list_.devices);
    }
    bool named_only() const { return page_.named_only(); }

    void set_discoverable(bool enabled) { page_.set_discoverable(enabled); }
    bool discoverable() const { return page_.discoverable(); }

    void set_alias(std::string alias) { page_.set_alias(std::move(alias)); }
    const std::string &alias() const { return page_.alias(); }

    void begin_alias_edit() { page_.begin_alias_edit(); }
    bool append_alias_text(const char *text) { return page_.append_alias_text(text); }
    bool erase_alias_character() { return page_.erase_alias_character(); }
    std::string sanitized_alias() const { return page_.sanitized_alias(); }
    const std::string &alias_input() const { return page_.alias_input(); }

    bool begin_feedback() { return page_.begin_feedback(); }
    bool finish_feedback() { return page_.finish_feedback(); }
    void cancel_feedback() { page_.cancel_feedback(); }
    bool feedback_pending() const { return page_.feedback_pending(); }

private:
    BluetoothSubPage sub_page_ = BluetoothSubPage::NONE;
    BluetoothSessionLoadState load_state_ = BluetoothSessionLoadState::CREATED;
    BluetoothStatusSnapshot status_;
    BluetoothListSnapshot list_;
    BluetoothPageModel page_;
};
