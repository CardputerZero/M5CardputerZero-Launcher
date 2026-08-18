#include "../main/ui/model/setup_page_model.hpp"

#include <cassert>

int main()
{
    assert(setup_input_context(SetupViewState::MAIN) == KBD_INPUT_CONTEXT_NAVIGATION);
    assert(setup_input_context(SetupViewState::WIFI_LIST) == KBD_INPUT_CONTEXT_NAVIGATION);
    assert(setup_input_context(SetupViewState::WIFI_PW) == KBD_INPUT_CONTEXT_TEXT);
    assert(setup_input_context(SetupViewState::WIFI_SSID) == KBD_INPUT_CONTEXT_TEXT);
    assert(setup_input_context(SetupViewState::BT_ALIAS) == KBD_INPUT_CONTEXT_TEXT);

    int root = 0;
    int stale_root = 0;
    static_assert(noexcept(setup_root_callback_allowed(&root, &root)));
    assert(setup_root_callback_allowed(&root, &root));
    assert(!setup_root_callback_allowed(&stale_root, &root));
    assert(!setup_root_callback_allowed(static_cast<int *>(nullptr), &root));

    int teardown_calls = 0;
    auto teardown_action = [] {};
    static_assert(noexcept(setup_teardown_step(teardown_action)));
    assert(setup_teardown_step([&] { ++teardown_calls; }));
    assert(!setup_teardown_step([&] {
        ++teardown_calls;
        throw 1;
    }));
    assert(setup_teardown_step([&] { ++teardown_calls; }));
    assert(teardown_calls == 3);

    SetupPageLifecycle lifecycle;
    assert(lifecycle.active());
    assert(lifecycle.begin_animation());
    assert(lifecycle.animating());
    assert(!lifecycle.begin_animation());
    lifecycle.finish_animation();
    assert(!lifecycle.animating());

    assert(lifecycle.begin_animation());
    lifecycle.cancel_animation();
    assert(lifecycle.active());
    assert(!lifecycle.animating());
    lifecycle.cancel_animation();
    assert(!lifecycle.animating());

    assert(lifecycle.begin_animation());
    lifecycle.root_deleted();
    assert(!lifecycle.active());
    assert(!lifecycle.animating());
    lifecycle.finish_animation();
    assert(!lifecycle.animating());
    assert(!lifecycle.begin_animation());
    lifecycle.root_deleted();

    SetupConfirmController confirmation;
    int calls = 0;
    assert(!confirmation.active());
    assert(!confirmation.resolve(true));

    confirmation.begin([&calls]() { ++calls; });
    assert(confirmation.active());
    assert(!confirmation.resolve(false));
    assert(!confirmation.active());
    assert(calls == 0);

    confirmation.begin([&calls]() { ++calls; });
    assert(confirmation.resolve(true));
    assert(!confirmation.active());
    assert(calls == 1);

    confirmation.begin([&calls]() { ++calls; });
    confirmation.cancel();
    assert(!confirmation.resolve(true));
    assert(calls == 1);

    SetupPageModel model;
    assert(model.view == SetupViewState::MAIN);
    assert(model.selected_index == SetupPageModel::DEFAULT_MENU_INDEX);
    assert(!model.enter_help());
    model.view = SetupViewState::SUB;
    assert(model.enter_help());
    assert(model.view == SetupViewState::HELP);
    assert(model.view != SetupViewState::WIFI_LIST);
    assert(!model.enter_help());
    assert(model.leave_help());
    assert(model.view == SetupViewState::SUB);
    assert(!model.leave_help());
    model.view = SetupViewState::MAIN;

    assert(model.move_main(-1, 5));
    assert(model.move_main(-1, 5));
    assert(model.move_main(-1, 5));
    assert(model.selected_index == 4);
    assert(model.move_main(1, 5));
    assert(model.selected_index == 0);
    assert(!model.move_main(1, 1));
    assert(!model.move_main(1, 0));

    model.enter_sub(8);
    assert(model.view == SetupViewState::SUB);
    assert(model.sub_selected_index == SetupPageModel::DEFAULT_CENTER_ROW);
    assert(model.move_sub(1, 8));
    assert(model.sub_selected_index == 4);

    model.enter_sub(2);
    assert(model.sub_selected_index == 1);
    assert(model.move_sub(1, 2));
    assert(model.sub_selected_index == 0);
    assert(model.move_sub(-1, 2));
    assert(model.sub_selected_index == 1);
    model.enter_sub(2, SetupPageModel::DEFAULT_CENTER_ROW, 0);
    assert(model.sub_selected_index == 0);
    model.enter_sub(2, SetupPageModel::DEFAULT_CENTER_ROW, 99);
    assert(model.sub_selected_index == 1);
    model.select_sub(0, 2);
    assert(model.sub_selected_index == 0);
    model.select_sub(99, 2);
    assert(model.sub_selected_index == 1);
    model.leave_to_main();
    assert(model.view == SetupViewState::MAIN);

    model.enter_value("Volume", {"100%", "50%", "0%"}, 99);
    assert(model.view == SetupViewState::VALUE_SELECT);
    assert(model.value_title == "Volume");
    assert(model.value_selected_index == 2);
    assert(!model.move_value(1));
    assert(model.move_value(-1));
    assert(model.value_selected_index == 1);
    model.leave_to_sub();
    assert(model.view == SetupViewState::SUB);

    model.enter_value("Empty", {}, 4);
    assert(model.value_selected_index == 0);
    assert(!model.move_value(-1));
}
