#include "settings_battery_calibration_page.hpp"
#include "settings_fonts.hpp"

#include "hal_lvgl_bsp.h"
#include "settings_async_dispatch.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>

#include <iterator>
#include <list>
#include <mutex>
#include <thread>
#include <utility>

namespace {
enum class BatteryOutcome { Success, Failed, TimedOut, Cancelled };
struct BatteryResult { BatteryOutcome outcome = BatteryOutcome::Failed; std::uint64_t generation = 0; int code = -1; };
class BatteryRequestCoordinator {
public:
    using Arguments = std::list<std::string>;
    using Post = std::function<bool(std::function<void()>)>;
    using Completion = std::function<void(const BatteryResult &)>;
    explicit BatteryRequestCoordinator(Post post) : post_(std::move(post)) {}
    ~BatteryRequestCoordinator() { shutdown(); }
    bool request(Arguments args, Completion completion) {
        if (!completion || !post_) return false;
        std::lock_guard<std::mutex> life(lifecycle_mutex_);
        std::thread previous; auto invocation = std::make_shared<Invocation>(); std::uint64_t generation;
        { std::lock_guard<std::mutex> lock(mutex_); if (shutting_down_ || pending_) return false; previous = std::move(worker_); pending_ = true; generation = ++next_generation_; active_generation_ = generation; active_invocation_ = invocation; }
        if (previous.joinable()) previous.join();
        try {
            auto alive = alive_;
            worker_ = std::thread([this, alive, invocation, generation, args = std::move(args), completion = std::move(completion)]() mutable {
                auto callback = [invocation](int code, std::string) { std::lock_guard<std::mutex> lock(invocation->mutex); if (invocation->completed) return; invocation->completed = true; invocation->code = code; invocation->condition.notify_all(); };
                try { cp0_signal_bq27220_api(std::move(args), std::move(callback)); } catch (...) { std::lock_guard<std::mutex> lock(invocation->mutex); if (!invocation->completed) { invocation->completed = true; invocation->code = -1; invocation->condition.notify_all(); } }
                BatteryResult result; result.generation = generation;
                std::unique_lock<std::mutex> lock(invocation->mutex); const bool completed = invocation->condition.wait_for(lock, std::chrono::milliseconds(1800), [&] { return invocation->completed || invocation->cancelled; });
                if (invocation->cancelled) result.outcome = BatteryOutcome::Cancelled;
                else if (!completed) result.outcome = BatteryOutcome::TimedOut;
                else { result.code = invocation->code; result.outcome = result.code == 0 ? BatteryOutcome::Success : BatteryOutcome::Failed; }
                lock.unlock();
                bool deliver; { std::lock_guard<std::mutex> state(mutex_); deliver = !shutting_down_ && alive->load() && active_generation_ == generation; }
                if (deliver) try { post_([alive, invocation, completion = std::move(completion), result]() mutable { if (!alive->load()) return; { std::lock_guard<std::mutex> lock(invocation->mutex); if (invocation->cancelled && result.outcome != BatteryOutcome::Cancelled) return; } try { completion(result); } catch (...) {} }); } catch (...) {}
                std::lock_guard<std::mutex> state(mutex_); if (active_generation_ == generation) { pending_ = false; active_invocation_.reset(); }
            });
        } catch (...) { std::lock_guard<std::mutex> lock(mutex_); pending_ = false; active_invocation_.reset(); return false; }
        return true;
    }
    void cancel() { std::shared_ptr<Invocation> invocation; { std::lock_guard<std::mutex> lock(mutex_); if (!pending_) return; invocation = active_invocation_; } if (!invocation) return; { std::lock_guard<std::mutex> lock(invocation->mutex); invocation->cancelled = true; } invocation->condition.notify_all(); }
    void shutdown() { std::lock_guard<std::mutex> life(lifecycle_mutex_); std::thread current; { std::lock_guard<std::mutex> lock(mutex_); if (shutting_down_) return; shutting_down_ = true; alive_->store(false); current = std::move(worker_); } cancel(); if (current.joinable()) current.join(); std::lock_guard<std::mutex> lock(mutex_); pending_ = false; active_invocation_.reset(); }
    bool pending() const { std::lock_guard<std::mutex> lock(mutex_); return pending_; }
    std::uint64_t generation() const { std::lock_guard<std::mutex> lock(mutex_); return active_generation_; }
private:
    struct Invocation { std::mutex mutex; std::condition_variable condition; bool completed = false; bool cancelled = false; int code = -1; };
    Post post_; mutable std::mutex mutex_; std::mutex lifecycle_mutex_; std::thread worker_; std::shared_ptr<std::atomic_bool> alive_ = std::make_shared<std::atomic_bool>(true); std::shared_ptr<Invocation> active_invocation_; std::uint64_t next_generation_ = 0, active_generation_ = 0; bool pending_ = false, shutting_down_ = false;
};
} // namespace

struct LvSettingBQCalibratePage3::State {
    explicit State(LvSettingBQCalibratePage3 *owner) : requests([owner](std::function<void()> task) { return owner && owner->post_to_lvgl(std::move(task)); }) {}
    BatteryRequestCoordinator requests;
    std::uint64_t active_generation = 0;
    AsyncToken async_token;
};

LvSettingBQCalibratePage3::LvSettingBQCalibratePage3()
    : state_(std::make_unique<State>(this))
{
}

LvSettingBQCalibratePage3::LvSettingBQCalibratePage3(
    lv_obj_t *parent,
    const NodeIter &parent_node)
    : LvSettingValuePage3Base(parent_node, {}),
      state_(std::make_unique<State>(this))
{
    initialize(parent);
}

LvSettingBQCalibratePage3::LvSettingBQCalibratePage3(
    lv_obj_t *parent,
    const NodeIter &parent_node,
    std::function<void()> back_callback)
    : LvSettingValuePage3Base(parent_node, std::move(back_callback)),
      state_(std::make_unique<State>(this))
{
    initialize(parent);
}

LvSettingBQCalibratePage3::~LvSettingBQCalibratePage3()
{
    if (state_) { state_->requests.cancel(); state_->requests.shutdown(); }
    cancel_async_tasks();
    result_label_ = nullptr;
}

void LvSettingBQCalibratePage3::create_ui(lv_obj_t *parent)
{
    LvSettingValuePage3Base::create_ui(parent);
    if (!state_) return;
    state_->async_token = async_token();
    if (!ComponensObj) return;

    result_label_ = lv_label_create(ComponensObj);
    if (!result_label_) return;
    lv_label_set_text(result_label_, "Ready");
    lv_obj_set_pos(result_label_, 8, 120);
    lv_obj_set_size(result_label_, 304, 15);
    lv_obj_set_style_text_color(result_label_, lv_color_hex(0xF0C850), LV_PART_MAIN);
    const lv_font_t *font = settings_fonts::sans(11, LV_FREETYPE_FONT_STYLE_BOLD);
    if (font) lv_obj_set_style_text_font(result_label_, font, LV_PART_MAIN);
    lv_label_set_long_mode(result_label_, LV_LABEL_LONG_CLIP);
}

int LvSettingBQCalibratePage3::initial_selection() const
{
    return 0;
}

SettingApiResult LvSettingBQCalibratePage3::activate_selected()
{
    const auto count = std::distance(parent_node().begin(), parent_node().end());
    if (selected_index < 0 || selected_index >= count) return SettingApiResult::NotHandled;
    if (state_->requests.pending()) return SettingApiResult::Pending;

    set_result("Sending calibration...");
    if (selected_index < 0 || selected_index > 3 ||
        !state_->requests.request({"Calibrate", std::to_string(selected_index)},
            [this](const BatteryResult &result) {
                handle_result(static_cast<int>(result.outcome), result.code,
                              result.generation);
            })) {
        set_result("Calibration unavailable");
        return SettingApiResult::Failure;
    }
    state_->active_generation = state_->requests.generation();
    return SettingApiResult::Pending;
}

bool LvSettingBQCalibratePage3::post_to_lvgl(std::function<void()> task)
{
    if (!state_ || !task || !state_->async_token.valid()) return false;
    return SettingsAsync::Dispatch::enqueue_from_callback(
        state_->async_token, std::move(task));
}

void LvSettingBQCalibratePage3::set_result(const std::string &text)
{
    if (result_label_) lv_label_set_text(result_label_, text.c_str());
}

void LvSettingBQCalibratePage3::handle_result(int outcome, int code,
                                              std::uint64_t generation)
{
    if (!state_ || generation != state_->active_generation)
        return;

    if (outcome == static_cast<int>(BatteryOutcome::Success) && code == 0) {
        set_result("Calibration complete");
    } else if (outcome == static_cast<int>(BatteryOutcome::TimedOut)) {
        set_result("Calibration timed out");
    } else if (outcome == static_cast<int>(BatteryOutcome::Cancelled)) {
        set_result("Calibration cancelled");
    } else {
        set_result("Calibration failed");
    }
}
