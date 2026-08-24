#pragma once

#include <cstddef>
#include <string>
#include <vector>

enum class BluetoothListMode { MANAGED, SCAN };

struct BluetoothDeviceState
{
    std::string address;
    std::string name;
    bool paired = false;
    bool connected = false;
};

struct BluetoothWireStatus
{
    bool powered = false;
    std::string address;
    bool discoverable = false;
    std::string alias;
};

struct BluetoothWireDevice
{
    std::string address;
    int rssi = 0;
    bool connected = false;
    bool paired = false;
    bool trusted = false;
    std::string name;
};

struct BluetoothListRow
{
    int device_index = -1;
    const char *title = nullptr;
    bool is_header = false;
};

class BluetoothPageModel
{
public:
    static constexpr size_t ALIAS_INPUT_LIMIT = 63;
    static constexpr const char *DEFAULT_ALIAS = "CardputerZero";

    static bool decode_status_record(const std::string &record, BluetoothWireStatus &status);
    static bool decode_device_record(const std::string &record, BluetoothWireDevice &device);

    void set_list_mode(BluetoothListMode mode);
    BluetoothListMode list_mode() const { return list_mode_; }

    void rebuild_rows(const std::vector<BluetoothDeviceState> &devices);
    void clear_selection() { list_selection_ = 0; }
    void select_next_device(int direction);
    int selected_device_index() const;
    int list_selection() const { return list_selection_; }
    const std::vector<BluetoothListRow> &rows() const { return rows_; }

    void set_named_only(bool enabled) { named_only_ = enabled; }
    bool named_only() const { return named_only_; }

    void set_discoverable(bool enabled) { discoverable_ = enabled; }
    bool discoverable() const { return discoverable_; }

    bool begin_feedback();
    bool finish_feedback();
    void cancel_feedback() { feedback_pending_ = false; }
    bool feedback_pending() const { return feedback_pending_; }

    void set_alias(std::string alias);
    const std::string &alias() const { return alias_; }
    void begin_alias_edit();
    bool append_alias_text(const char *text);
    bool erase_alias_character();
    std::string sanitized_alias() const;
    const std::string &alias_input() const { return alias_input_; }

private:
    static bool alias_character_allowed(unsigned char character);
    static std::string normalized_mac_text(const std::string &text);
    bool should_hide_device(const BluetoothDeviceState &device) const;

    BluetoothListMode list_mode_ = BluetoothListMode::MANAGED;
    int list_selection_ = 0;
    std::vector<BluetoothListRow> rows_;
    bool named_only_ = true;
    bool discoverable_ = false;
    bool feedback_pending_ = false;
    std::string alias_ = DEFAULT_ALIAS;
    std::string alias_input_;
};
