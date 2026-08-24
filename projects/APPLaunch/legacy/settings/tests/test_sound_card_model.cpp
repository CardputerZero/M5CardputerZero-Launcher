#include "../main/ui/model/sound_card_model.hpp"

#include <cassert>
#include <limits>
#include <stdexcept>

int main()
{
    int timer = 0;
    int stale_timer = 0;
    int input_label = 0;
    int hint_label = 0;
    int screen = 0;
    int stale_screen = 0;
    int child = 0;
    assert(setting::sound_card_cursor_callback_ready(&timer, &timer, &input_label));
    assert(!setting::sound_card_cursor_callback_ready(&stale_timer, &timer, &input_label));
    assert(!setting::sound_card_cursor_callback_ready(&timer, &timer, nullptr));
    static_assert(noexcept(setting::sound_card_cursor_callback_allowed(
        static_cast<int *>(nullptr), static_cast<int *>(nullptr),
        static_cast<int *>(nullptr), false)));
    assert(setting::sound_card_cursor_callback_allowed(
        &timer, &timer, &input_label, true));
    assert(!setting::sound_card_cursor_callback_allowed(
        &stale_timer, &timer, &input_label, true));
    assert(!setting::sound_card_cursor_callback_allowed(
        &timer, &timer, nullptr, true));
    assert(!setting::sound_card_cursor_callback_allowed(
        &timer, &timer, &input_label, false));
    static_assert(noexcept(setting::sound_card_transition_timer_callback_allowed(
        static_cast<int *>(nullptr), static_cast<int *>(nullptr),
        static_cast<int *>(nullptr), false)));
    assert(setting::sound_card_transition_timer_callback_allowed(
        &timer, &timer, &screen, true));
    assert(!setting::sound_card_transition_timer_callback_allowed(
        &stale_timer, &timer, &screen, true));
    assert(!setting::sound_card_transition_timer_callback_allowed(
        &timer, &timer, static_cast<int *>(nullptr), true));
    assert(!setting::sound_card_transition_timer_callback_allowed(
        &timer, &timer, &screen, false));
    assert(setting::sound_card_screen_delete_is_direct(&screen, &screen));
    assert(!setting::sound_card_screen_delete_is_direct(&child, &screen));
    assert(!setting::sound_card_screen_delete_is_direct(
        static_cast<int *>(nullptr), &screen));
    static_assert(noexcept(setting::sound_card_transition_screen_delete_matches(
        static_cast<int *>(nullptr), static_cast<int *>(nullptr),
        static_cast<int *>(nullptr))));
    assert(setting::sound_card_transition_screen_delete_matches(
        &screen, &screen, &screen));
    assert(!setting::sound_card_transition_screen_delete_matches(
        &child, &screen, &screen));
    assert(!setting::sound_card_transition_screen_delete_matches(
        &stale_screen, &stale_screen, &screen));
    assert(!setting::sound_card_transition_screen_delete_matches(
        &screen, &screen, static_cast<int *>(nullptr)));
    static_assert(noexcept(setting::sound_card_owned_label_delete_matches(
        static_cast<int *>(nullptr), static_cast<int *>(nullptr),
        static_cast<int *>(nullptr))));
    assert(setting::sound_card_owned_label_delete_matches(
        &input_label, &input_label, &input_label));
    assert(!setting::sound_card_owned_label_delete_matches(
        &child, &input_label, &input_label));
    assert(!setting::sound_card_owned_label_delete_matches(
        &input_label, &input_label, &child));
    assert(!setting::sound_card_owned_label_delete_matches(
        static_cast<int *>(nullptr), &input_label, &input_label));
    assert(setting::sound_card_owned_label_delete_matches(
        &hint_label, &hint_label, &hint_label));
    assert(!setting::sound_card_owned_label_delete_matches(
        &child, &hint_label, &hint_label));
    assert(!setting::sound_card_owned_label_delete_matches(
        &hint_label, &hint_label, &input_label));

    int backend_calls = 0;
    assert(setting::sound_card_invoke_backend([&] { ++backend_calls; }));
    assert(backend_calls == 1);
    assert(!setting::sound_card_invoke_backend([] { throw std::runtime_error("backend"); }));

    std::vector<int> current{1, 2};
    assert(!setting::sound_card_commit_if_success(false, std::vector<int>{3}, current));
    assert((current == std::vector<int>{1, 2}));
    assert(setting::sound_card_commit_if_success(true, std::vector<int>{4, 5}, current));
    assert((current == std::vector<int>{4, 5}));

    using setting::SoundCardModel;
    using setting::SoundCardTransitionModel;
    using setting::SoundControlInfo;

    const auto cards = SoundCardModel::parse_cards(
        "0\tCard 0: Built-in Audio\ninvalid\n2\tCard 2: USB Audio\n");
    assert(cards.size() == 2);
    assert(cards[0].index == 0 && cards[0].name == "Card 0: Built-in Audio");
    assert(cards[1].index == 2 && cards[1].name == "Card 2: USB Audio");
    const auto bounded_cards = SoundCardModel::parse_cards(
        "2junk\tbad\n-1\tnegative\n2147483648\toverflow\n2147483647\tvalid\n");
    assert(bounded_cards.size() == 1);
    assert(bounded_cards[0].index == std::numeric_limits<int>::max());

    const auto controls = SoundCardModel::parse_controls(
        "Master\tINTEGER\t0\t100\t1\tPlayback: 45 [45%]\t45\n"
        "bad\trow\n"
        "Input Source\tENUMERATED\t0\t0\t1\tItem0: 'Mic'\t0\n");
    assert(controls.size() == 2);
    assert(controls[0].name == "Master");
    assert(controls[0].min_value == 0 && controls[0].max_value == 100);
    assert(controls[0].current_value == 45);
    assert(controls[1].current_text == "Item0: 'Mic'");
    const auto bounded_controls = SoundCardModel::parse_controls(
        "bad-min\tINTEGER\t0junk\t100\t1\tPlayback: 1\t1\n"
        "bad-max\tINTEGER\t0\t2147483648\t1\tPlayback: 1\t1\n"
        "bad-step\tINTEGER\t0\t100\t1x\tPlayback: 1\t1\n"
        "bad-current\tINTEGER\t0\t100\t1\tPlayback: 1\t999999999999999999\n");
    assert(bounded_controls.empty());

    const SoundControlInfo fallback{"Master", "INTEGER", 5, 95, 1,
                                    "Playback: 20 [20%]", 20};
    const auto detail = SoundCardModel::parse_detail(
        "  Capabilities: pvolume\n"
        "  Limits: Playback 0 - 100\n"
        "  Front Left: Playback 67 [67%]\n",
        fallback);
    assert(detail.name == "Master" && detail.type == "INTEGER");
    assert(detail.min_value == 0 && detail.max_value == 100);
    assert(detail.current_text == "Front Left: Playback 67 [67%]");
    assert(detail.current_value == 67);

    const auto overflow_detail = SoundCardModel::parse_detail(
        "Capabilities: pvolume\n"
        "Limits: Playback -2147483649 - 2147483648\n"
        "Front Left: Playback 999999999999999999 [100%]\n",
        fallback);
    assert(overflow_detail.min_value == fallback.min_value);
    assert(overflow_detail.max_value == fallback.max_value);
    assert(overflow_detail.current_value == 0);

    const auto enum_detail = SoundCardModel::parse_detail(
        "Capabilities: enum\nItem0: 'Headphones'\n", controls[1]);
    assert(enum_detail.type == "ENUMERATED");
    assert(enum_detail.current_text == "Item0: 'Headphones'");

    assert(SoundCardModel::clamp_value(-1, fallback) == 5);
    assert(SoundCardModel::clamp_value(50, fallback) == 50);
    assert(SoundCardModel::clamp_value(120, fallback) == 95);
    assert(SoundCardModel::clamp_value(120, controls[1]) == 120);
    assert(SoundCardModel::parse_value("-2147483648", 7) == std::numeric_limits<int>::min());
    assert(SoundCardModel::parse_value("2147483647", 7) == std::numeric_limits<int>::max());
    assert(SoundCardModel::parse_value("2147483648", 7) == 7);
    assert(SoundCardModel::parse_value("12junk", 7) == 7);
    assert(SoundCardModel::parse_value("+12", 7) == 7);

    SoundCardTransitionModel transition;
    assert(!transition.pending());
    assert(transition.begin());
    assert(transition.pending());
    assert(!transition.begin());
    assert(transition.complete());
    assert(!transition.pending());
    assert(!transition.complete());
    assert(transition.begin());
    transition.cancel();
    assert(!transition.pending());
}
