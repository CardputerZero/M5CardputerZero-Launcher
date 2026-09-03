#include "lora_page_model.hpp"

#include <utility>

void LoraPageModel::reset(bool hardware_ready)
{
    view_ = hardware_ready ? LoraView::MESSAGES : LoraView::INFO;
    tx_input_.clear();
    send_status_.clear();
    messages_.clear();
}

void LoraPageModel::begin_send(char first_character)
{
    view_ = LoraView::SEND;
    tx_input_.clear();
    send_status_.clear();
    if (first_character >= 0x20 && first_character <= 0x7e) tx_input_.push_back(first_character);
}

void LoraPageModel::cancel_send()
{
    view_ = LoraView::MESSAGES;
    tx_input_.clear();
    send_status_.clear();
}

bool LoraPageModel::append_character(char character)
{
    if (character < 0x20 || character > 0x7e || tx_input_.size() >= TX_INPUT_LIMIT) return false;
    tx_input_.push_back(character);
    send_status_.clear();
    return true;
}

bool LoraPageModel::erase_character()
{
    if (tx_input_.empty()) return false;
    tx_input_.pop_back();
    send_status_.clear();
    return true;
}

void LoraPageModel::complete_send()
{
    view_ = LoraView::MESSAGES;
    tx_input_.clear();
    send_status_.clear();
}

void LoraPageModel::append_message(std::string text, bool outgoing, float rssi, float snr)
{
    if (text.empty()) text = "<empty>";
    if (messages_.size() >= MESSAGE_HISTORY_LIMIT) messages_.pop_front();
    messages_.push_back({std::move(text), outgoing, rssi, snr});
}
