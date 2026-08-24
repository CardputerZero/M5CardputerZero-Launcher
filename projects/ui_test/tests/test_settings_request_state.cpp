#include "settings_tree_types.hpp"

#include <cassert>

int main()
{
    SettingRequestState state;
    assert(state.state() == SettingComponentState::Read);
    assert(!state.pending());

    const uint64_t first_generation = state.begin_activation();
    assert(first_generation != 0);
    assert(state.state() == SettingComponentState::Activate);
    assert(state.pending());
    assert(state.begin_activation() == 0);
    assert(state.mark_pending(first_generation));
    assert(state.state() == SettingComponentState::Pending);
    assert(!state.mark_pending(first_generation));
    assert(state.mark_success(first_generation));
    assert(state.state() == SettingComponentState::Success);
    assert(!state.pending());
    assert(!state.mark_failure(first_generation));

    const uint64_t second_generation = state.begin_activation();
    assert(second_generation != 0);
    assert(second_generation != first_generation);
    assert(!state.mark_success(first_generation));
    assert(state.mark_failure(second_generation));
    assert(state.state() == SettingComponentState::Failure);

    const uint64_t third_generation = state.begin_activation();
    assert(third_generation != 0);
    assert(state.mark_pending(third_generation));
    assert(state.mark_cancelled(third_generation));
    assert(state.state() == SettingComponentState::Cancelled);

    const SettingEntry entry = SettingEntry::make_async(
        "Async", [](int, void *) { return SettingApiResult::Pending; });
    assert(entry.has_api());
    assert(entry.Async_api);
    assert(entry.activation_policy == SettingActivationPolicy::WaitForResult);

    return 0;
}
