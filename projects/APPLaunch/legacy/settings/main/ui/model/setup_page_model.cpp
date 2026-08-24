#include "setup_page_model.hpp"

#include <algorithm>
#include <utility>

void SetupConfirmController::begin(Action action)
{
    action_ = std::move(action);
}

void SetupConfirmController::cancel()
{
    action_ = nullptr;
}

bool SetupConfirmController::active() const
{
    return static_cast<bool>(action_);
}

bool SetupConfirmController::resolve(bool confirmed)
{
    Action action = std::move(action_);
    action_ = nullptr;
    if (!confirmed || !action) return false;
    action();
    return true;
}

bool SetupPageLifecycle::begin_animation()
{
    if (!active_ || animating_) return false;
    animating_ = true;
    return true;
}

void SetupPageLifecycle::finish_animation()
{
    if (active_) animating_ = false;
}

void SetupPageLifecycle::cancel_animation()
{
    animating_ = false;
}

void SetupPageLifecycle::root_deleted()
{
    active_ = false;
    animating_ = false;
}

int SetupPageModel::clamp_index(int index, int item_count)
{
    if (item_count <= 0) return 0;
    return std::max(0, std::min(index, item_count - 1));
}

bool SetupPageModel::move_main(int direction, int item_count)
{
    if (item_count <= 1 || direction == 0) return false;
    int next = (selected_index + direction) % item_count;
    if (next < 0) next += item_count;
    if (next == selected_index) return false;
    selected_index = next;
    return true;
}

void SetupPageModel::enter_sub(int item_count, int center_row)
{
    view = SetupViewState::SUB;
    sub_selected_index = clamp_index(std::min(center_row, item_count - 1), item_count);
}

bool SetupPageModel::move_sub(int direction, int item_count)
{
    if (item_count <= 1 || direction == 0) return false;
    int next = (sub_selected_index + direction) % item_count;
    if (next < 0) next += item_count;
    if (next == sub_selected_index) return false;
    sub_selected_index = next;
    return true;
}

void SetupPageModel::select_sub(int index, int item_count)
{
    sub_selected_index = clamp_index(index, item_count);
}

void SetupPageModel::enter_value(std::string title, std::vector<std::string> options, int selected)
{
    value_title = std::move(title);
    value_options = std::move(options);
    value_selected_index = clamp_index(selected, static_cast<int>(value_options.size()));
    view = SetupViewState::VALUE_SELECT;
}

bool SetupPageModel::enter_help()
{
    if (view != SetupViewState::SUB) return false;
    view = SetupViewState::HELP;
    return true;
}

bool SetupPageModel::leave_help()
{
    if (view != SetupViewState::HELP) return false;
    view = SetupViewState::SUB;
    return true;
}

bool SetupPageModel::move_value(int direction)
{
    int next = clamp_index(value_selected_index + direction,
                           static_cast<int>(value_options.size()));
    if (next == value_selected_index) return false;
    value_selected_index = next;
    return true;
}

void SetupPageModel::leave_to_main() { view = SetupViewState::MAIN; }
void SetupPageModel::leave_to_sub() { view = SetupViewState::SUB; }
