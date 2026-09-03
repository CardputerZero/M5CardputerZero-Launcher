#pragma once

#include <cstddef>
#include <deque>
#include <string>
#include <utility>

enum class LoraView { MESSAGES, INFO, SEND };

struct LoraChatMessage {
    std::string text;
    bool outgoing = false;
    float rssi = 0.0f;
    float snr = 0.0f;
};

class LoraPageModel
{
public:
    static constexpr size_t MESSAGE_HISTORY_LIMIT = 64;
    static constexpr size_t TX_INPUT_LIMIT = 127;

    void reset(bool hardware_ready);
    void set_view(LoraView view) { view_ = view; }
    LoraView view() const { return view_; }

    void begin_send(char first_character = 0);
    void cancel_send();
    bool append_character(char character);
    bool erase_character();
    void set_send_status(std::string status) { send_status_ = std::move(status); }
    void complete_send();

    void append_message(std::string text, bool outgoing, float rssi, float snr);

    const std::string &tx_input() const { return tx_input_; }
    const std::string &send_status() const { return send_status_; }
    const std::deque<LoraChatMessage> &messages() const { return messages_; }

private:
    LoraView view_ = LoraView::MESSAGES;
    std::string tx_input_;
    std::string send_status_;
    std::deque<LoraChatMessage> messages_;
};
