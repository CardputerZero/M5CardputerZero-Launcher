#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace setting {

template <typename EventTarget, typename CurrentTarget, typename TrackedObject>
constexpr bool developer_delete_callback_allowed(EventTarget event_target,
                                                  CurrentTarget current_target,
                                                  TrackedObject tracked_object) noexcept
{
    return event_target && event_target == current_target &&
           event_target == tracked_object;
}

template <typename OwnerHandle, typename PageHandle>
constexpr bool developer_async_completion_allowed(bool token_completed,
                                                  OwnerHandle owner,
                                                  PageHandle page) noexcept
{
    return token_completed && owner && page;
}

class DeveloperPairViewBuildContract
{
public:
    static constexpr std::size_t REQUIRED_LABELS = 4;

    void label_created() { ++created_labels_; }
    bool ready() const { return created_labels_ == REQUIRED_LABELS; }

private:
    std::size_t created_labels_ = 0;
};

class DeveloperOverlayLifecycleModel
{
public:
    bool open();
    bool close();
    bool active() const { return active_; }

private:
    bool active_ = false;
};

class DeveloperPageModel
{
public:
    static constexpr std::size_t MAX_PAIR_KEY_BYTES = 2048;
    static constexpr std::size_t PAIR_PREVIEW_BYTES = 40;

    void begin_pairing(bool enable_after_pair);
    void clear_pairing();
    bool append_pairing_text(std::string_view text);
    bool erase_pairing_character();

    const std::string &pair_key() const { return pair_key_; }
    bool enable_after_pair() const { return enable_after_pair_; }
    std::string pairing_preview() const;

    void reset_authorization_selection(std::size_t count);
    void reconcile_authorization_count(std::size_t count);
    bool select_previous_authorization();
    bool select_next_authorization(std::size_t count);
    std::size_t authorization_selection() const { return authorization_selection_; }
    std::size_t authorization_window_start(std::size_t visible_rows = 3) const;

private:
    std::string pair_key_;
    bool enable_after_pair_ = false;
    std::size_t authorization_selection_ = 0;
};

} // namespace setting
