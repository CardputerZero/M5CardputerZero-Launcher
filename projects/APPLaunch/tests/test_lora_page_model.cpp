#include "../main/ui/model/lora_page_model.hpp"
#include "../main/ui/model/lora_page_contract.hpp"

#include <cassert>
#include <string>

int main()
{
    static_assert(noexcept(lora_poll_callback_allowed(true, true)));
    static_assert(lora_poll_callback_allowed(true, true));
    static_assert(!lora_poll_callback_allowed(false, true));
    static_assert(!lora_poll_callback_allowed(true, false));

    int animation_target = 1;
    static_assert(noexcept(lora_animation_callback_allowed(&animation_target)));
    assert(lora_animation_callback_allowed(&animation_target));
    assert(!lora_animation_callback_allowed(static_cast<int *>(nullptr)));

    int delete_target = 1;
    int bubbled_target = 2;
    assert(lora_owned_delete_callback_allowed(&delete_target, &delete_target));
    assert(!lora_owned_delete_callback_allowed(&delete_target, &bubbled_target));
    assert(!lora_owned_delete_callback_allowed(
        static_cast<int *>(nullptr), &delete_target));

    int action_button = 3;
    int stale_button = 4;
    static_assert(noexcept(lora_send_action_callback_allowed(
        static_cast<int *>(nullptr), static_cast<int *>(nullptr), false, false)));
    assert(lora_send_action_callback_allowed(
        &action_button, &action_button, true, true));
    assert(!lora_send_action_callback_allowed(
        &stale_button, &action_button, true, true));
    assert(!lora_send_action_callback_allowed(
        &action_button, &action_button, false, true));
    assert(!lora_send_action_callback_allowed(
        &action_button, &action_button, true, false));

    int page_root = 5;
    int stale_page = 6;
    static_assert(noexcept(lora_page_event_callback_allowed(
        static_cast<int *>(nullptr), static_cast<int *>(nullptr), false)));
    assert(lora_page_event_callback_allowed(&page_root, &page_root, true));
    assert(!lora_page_event_callback_allowed(&stale_page, &page_root, true));
    assert(!lora_page_event_callback_allowed(&page_root, &page_root, false));

    assert(lora_info_response_valid(0, 32, 32));
    assert(!lora_info_response_valid(-1, 32, 32));
    assert(!lora_info_response_valid(0, 31, 32));

    int handle = 1;
    int *present = &handle;
    int *missing = nullptr;
    assert(lora_page_ui_ready(present, present, present, present, present, present));
    assert(!lora_page_ui_ready(missing, present, present, present, present, present));
    assert(!lora_page_ui_ready(present, present, missing, present, present, present));
    assert(!lora_page_ui_ready(present, present, present, present, present, missing));
    assert(lora_page_controls_ready(present, present, present));
    assert(!lora_page_controls_ready(present, missing, present));

    LoraPageModel model;
    model.reset(true);
    assert(model.view() == LoraView::MESSAGES);
    assert(model.messages().empty());

    model.reset(false);
    assert(model.view() == LoraView::INFO);

    model.begin_send('A');
    assert(model.view() == LoraView::SEND);
    assert(model.tx_input() == "A");
    assert(!model.append_character('\n'));
    assert(model.tx_input() == "A");
    assert(model.append_character('B'));
    assert(model.erase_character());
    assert(model.tx_input() == "A");
    model.cancel_send();
    assert(model.view() == LoraView::MESSAGES);
    assert(model.tx_input().empty());

    model.begin_send();
    for (size_t index = 0; index < LoraPageModel::TX_INPUT_LIMIT; ++index)
        assert(model.append_character('x'));
    assert(!model.append_character('y'));
    assert(model.tx_input().size() == LoraPageModel::TX_INPUT_LIMIT);

    model.set_send_status("sending");
    model.complete_send();
    assert(model.view() == LoraView::MESSAGES);
    assert(model.tx_input().empty());
    assert(model.send_status().empty());

    model.append_message("", false, -81.0f, 7.5f);
    assert(model.messages().back().text == "<empty>");
    for (size_t index = 0; index < LoraPageModel::MESSAGE_HISTORY_LIMIT + 3; ++index)
        model.append_message(std::to_string(index), true, 0.0f, 0.0f);
    assert(model.messages().size() == LoraPageModel::MESSAGE_HISTORY_LIMIT);
    assert(model.messages().front().text == "3");
    assert(model.messages().back().text == "66");
}
