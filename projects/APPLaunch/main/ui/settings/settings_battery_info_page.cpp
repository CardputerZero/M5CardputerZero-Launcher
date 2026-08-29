#include "settings_battery_info_page.hpp"

#include "cp0_font_service.hpp"
#include "settings_async_dispatch.hpp"
#include "settings_battery_info_model.hpp"
#include "hal_lvgl_bsp.h"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <list>
#include <mutex>
#include <thread>
#include <utility>

namespace {

enum class BatteryOutcome { Success, Failed, TimedOut, Cancelled };
struct BatteryResult {
    BatteryOutcome outcome = BatteryOutcome::Failed;
    std::uint64_t generation = 0;
    int code = -1;
    std::string payload;
};

class BatteryRequestCoordinator {
public:
    using Arguments = std::list<std::string>;
    using Post = std::function<bool(std::function<void()>)>;
    using Completion = std::function<void(const BatteryResult &)>;

    explicit BatteryRequestCoordinator(Post post,
                                       std::chrono::milliseconds timeout = std::chrono::milliseconds(1800))
        : post_(std::move(post)), timeout_(timeout) {}
    ~BatteryRequestCoordinator() { shutdown(); }
    BatteryRequestCoordinator(const BatteryRequestCoordinator &) = delete;
    BatteryRequestCoordinator &operator=(const BatteryRequestCoordinator &) = delete;

    bool request(Arguments arguments, Completion completion) {
        if (!completion || !post_) return false;
        std::lock_guard<std::mutex> life(lifecycle_mutex_);
        std::thread previous;
        auto invocation = std::make_shared<Invocation>();
        std::uint64_t generation;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (shutting_down_ || pending_) return false;
            previous = std::move(worker_);
            pending_ = true;
            generation = ++next_generation_;
            active_generation_ = generation;
            active_invocation_ = invocation;
        }
        if (previous.joinable()) previous.join();
        try {
            auto alive = alive_;
            worker_ = std::thread([this, alive, invocation, generation,
                                   arguments = std::move(arguments),
                                   completion = std::move(completion)]() mutable {
                auto callback = [invocation](int code, std::string payload) {
                    std::lock_guard<std::mutex> lock(invocation->mutex);
                    if (invocation->completed) return;
                    invocation->completed = true;
                    invocation->code = code;
                    invocation->payload = std::move(payload);
                    invocation->condition.notify_all();
                };
                try { cp0_signal_bq27220_api(std::move(arguments), std::move(callback)); }
                catch (...) {
                    std::lock_guard<std::mutex> lock(invocation->mutex);
                    if (!invocation->completed) { invocation->completed = true; invocation->code = -1; invocation->payload = "battery api exception"; invocation->condition.notify_all(); }
                }
                BatteryResult result;
                result.generation = generation;
                std::unique_lock<std::mutex> lock(invocation->mutex);
                const bool completed = invocation->condition.wait_for(lock, timeout_, [&] { return invocation->completed || invocation->cancelled; });
                if (invocation->cancelled) result.outcome = BatteryOutcome::Cancelled;
                else if (!completed) { invocation->completed = true; result.outcome = BatteryOutcome::TimedOut; }
                else { result.code = invocation->code; result.payload = invocation->payload; result.outcome = result.code == 0 ? BatteryOutcome::Success : BatteryOutcome::Failed; }
                lock.unlock();
                bool deliver;
                { std::lock_guard<std::mutex> state(mutex_); deliver = !shutting_down_ && alive->load() && active_generation_ == generation; }
                if (deliver) {
                    try { post_([alive, invocation, completion = std::move(completion), result]() mutable {
                        if (!alive->load()) return;
                        {
                            std::lock_guard<std::mutex> lock(invocation->mutex);
                            if (invocation->cancelled && result.outcome != BatteryOutcome::Cancelled) return;
                        }
                        try { completion(result); } catch (...) {}
                    }); } catch (...) {}
                }
                std::lock_guard<std::mutex> state(mutex_);
                if (active_generation_ == generation) { pending_ = false; active_invocation_.reset(); }
            });
        } catch (...) {
            std::lock_guard<std::mutex> lock(mutex_);
            pending_ = false; active_invocation_.reset();
            return false;
        }
        return true;
    }
    void cancel() {
        std::shared_ptr<Invocation> invocation;
        { std::lock_guard<std::mutex> lock(mutex_); if (!pending_) return; invocation = active_invocation_; }
        if (!invocation) return;
        { std::lock_guard<std::mutex> lock(invocation->mutex); invocation->cancelled = true; }
        invocation->condition.notify_all();
    }
    void shutdown() {
        std::lock_guard<std::mutex> life(lifecycle_mutex_);
        std::thread current;
        { std::lock_guard<std::mutex> lock(mutex_); if (shutting_down_) return; shutting_down_ = true; alive_->store(false); current = std::move(worker_); }
        cancel();
        if (current.joinable()) current.join();
        std::lock_guard<std::mutex> lock(mutex_); pending_ = false; active_invocation_.reset();
    }
    bool pending() const { std::lock_guard<std::mutex> lock(mutex_); return pending_; }
    std::uint64_t generation() const { std::lock_guard<std::mutex> lock(mutex_); return active_generation_; }

private:
    struct Invocation { std::mutex mutex; std::condition_variable condition; bool completed = false; bool cancelled = false; int code = -1; std::string payload; };
    Post post_;
    std::chrono::milliseconds timeout_;
    mutable std::mutex mutex_;
    std::mutex lifecycle_mutex_;
    std::thread worker_;
    std::shared_ptr<std::atomic_bool> alive_ = std::make_shared<std::atomic_bool>(true);
    std::shared_ptr<Invocation> active_invocation_;
    std::uint64_t next_generation_ = 0;
    std::uint64_t active_generation_ = 0;
    bool pending_ = false;
    bool shutting_down_ = false;
};

} // namespace

struct LvSettingBatteryInfoPage3::State {
    explicit State(LvSettingBatteryInfoPage3 *owner)
        : requests([owner](std::function<void()> task) {
              return owner && owner->post_to_lvgl(std::move(task));
          })
    {
    }

    NodeIter parent_node{};
    BatteryRequestCoordinator requests;
    SettingsBatteryInfoModel model;
    std::array<lv_obj_t *, static_cast<std::size_t>(SettingsBatteryInfoModel::LabelMetric::Count)> value_labels{};
    lv_obj_t *title_label  = nullptr;
    lv_obj_t *status_label = nullptr;
    lv_obj_t *hint_label   = nullptr;
    lv_timer_t *refresh_timer = nullptr;
    std::uint64_t active_generation = 0;
    SettingsAsync::Dispatch::Token async_token;
    std::string status_message = "Battery unavailable";
};

LvSettingBatteryInfoPage3::LvSettingBatteryInfoPage3()
    : state_(std::make_unique<State>(this))
{
}

LvSettingBatteryInfoPage3::LvSettingBatteryInfoPage3(
    lv_obj_t *parent,
    const NodeIter &parent_node,
    std::function<void()> back_callback)
    : state_(std::make_unique<State>(this))
{
    state_->parent_node = parent_node;
    LeaveSelfPage = std::move(back_callback);
    create_ui(parent);
}

LvSettingBatteryInfoPage3::~LvSettingBatteryInfoPage3()
{
    stop_refresh_timer();
    if (state_) {
        state_->requests.cancel();
        state_->requests.shutdown();
    }
    cancel_async_tasks();
    if (ComponensObj) {
        lv_anim_del(ComponensObj, nullptr);
        lv_obj_delete(ComponensObj);
        ComponensObj = nullptr;
    }
}

void LvSettingBatteryInfoPage3::AnimateNextIn(std::function<void()> animate_over_func)
{
    if (animate_over_func) animate_over_func();
}

void LvSettingBatteryInfoPage3::AnimateNextOut(std::function<void()> animate_over_func)
{
    if (animate_over_func) animate_over_func();
}

void LvSettingBatteryInfoPage3::LoadNextPage()
{
}

void LvSettingBatteryInfoPage3::LeaveNextPage()
{
    if (state_) state_->requests.cancel();
    if (LeaveSelfPage) LeaveSelfPage();
}

void LvSettingBatteryInfoPage3::create_ui(lv_obj_t *parent)
{
    if (!parent || !state_) return;

    const bool dispatch_ready = ensure_async_dispatch();
    state_->async_token = async_token();

    ComponensObj = lv_obj_create(parent);
    if (!ComponensObj) return;
    lv_obj_set_size(ComponensObj, metric(LayoutMetric::ScreenW), metric(LayoutMetric::ScreenH));
    lv_obj_set_pos(ComponensObj, 0, 0);
    lv_obj_set_style_bg_color(ComponensObj, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ComponensObj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(ComponensObj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(ComponensObj, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(ComponensObj, 0, LV_PART_MAIN);
    lv_obj_remove_flag(ComponensObj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(ComponensObj, LV_OBJ_FLAG_SCROLLABLE);
    DComponens::lvgl_bind_event(
        ComponensObj,
        LV_EVENT_KEY,
        nullptr,
        std::bind(&LvSettingBatteryInfoPage3::handle_key_event,
                  this,
                  std::placeholders::_1));

    state_->title_label = create_label(
        metric(LayoutMetric::TitleX),
        metric(LayoutMetric::TitleY),
        metric(LayoutMetric::TitleW),
        metric(LayoutMetric::TitleH),
        0xFFFFFF,
        13);
    if (state_->title_label) lv_label_set_text(state_->title_label, "Battery Info");

    for (std::size_t index = 0; index < state_->value_labels.size(); ++index) {
        state_->value_labels[index] = create_label(
            metric(LayoutMetric::ValueX),
            metric(LayoutMetric::ValueY) + static_cast<int>(index) * metric(LayoutMetric::ValueGap),
            metric(LayoutMetric::ValueW),
            metric(LayoutMetric::ValueH),
            index == 0 ? 0xFFFFFF : 0xB8B8B8,
            index == 0 ? 12 : 11);
    }

    state_->status_label = create_label(
        metric(LayoutMetric::StatusX),
        metric(LayoutMetric::StatusY),
        metric(LayoutMetric::StatusW),
        metric(LayoutMetric::StatusH),
        0xF0C850,
        11);
    state_->hint_label = create_label(
        metric(LayoutMetric::HintX),
        metric(LayoutMetric::HintY),
        metric(LayoutMetric::HintW),
        metric(LayoutMetric::HintH),
        0x777777,
        10);
    if (state_->hint_label) lv_label_set_text(state_->hint_label, "OK: refresh  ESC: back");

    state_->refresh_timer = lv_timer_create(
        refresh_timer_cb,
        static_cast<uint32_t>(metric(LayoutMetric::RefreshIntervalMs)),
        this);
    if (state_->refresh_timer) lv_timer_ready(state_->refresh_timer);
    else state_->status_message = "Refresh unavailable";
    if (!dispatch_ready || !state_->async_token.valid())
        state_->status_message = "Async refresh unavailable";

    render();
    if (dispatch_ready && state_->async_token.valid()) request_read();
}

bool LvSettingBatteryInfoPage3::post_to_lvgl(std::function<void()> task)
{
    if (!state_ || !task || !state_->async_token.valid()) return false;
    return SettingsAsync::Dispatch::enqueue_from_callback(
        state_->async_token,
        std::move(task));
}

void LvSettingBatteryInfoPage3::refresh_timer_cb(lv_timer_t *timer)
{
    auto *self = timer
                    ? static_cast<LvSettingBatteryInfoPage3 *>(lv_timer_get_user_data(timer))
                    : nullptr;
    if (self) self->request_read();
}

lv_obj_t *LvSettingBatteryInfoPage3::create_label(int x,
                                                  int y,
                                                  int width,
                                                  int height,
                                                  std::uint32_t color,
                                                  int font_size)
{
    if (!ComponensObj) return nullptr;
    lv_obj_t *label = lv_label_create(ComponensObj);
    if (!label) return nullptr;
    lv_label_set_text(label, "");
    lv_obj_set_pos(label, x, y);
    lv_obj_set_size(label, width, height);
    lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
    const lv_font_t *font = cp0_fonts().get(
        "Montserrat-Bold.ttf",
        font_size,
        LV_FREETYPE_FONT_STYLE_BOLD);
    if (font) lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    return label;
}

void LvSettingBatteryInfoPage3::request_read()
{
    if (!state_ || !ComponensObj || !state_->async_token.valid() || state_->requests.pending()) return;

    if (state_->model.valid()) state_->model.set_status("Reading battery...");
    else state_->model.invalidate("Reading battery...");
    state_->status_message = "Reading battery...";
    render();

    if (!state_->requests.request({"Read"}, [this](const BatteryResult &result) {
            handle_read_result(static_cast<int>(result.outcome), result.code,
                               result.generation, result.payload);
        })) {
        state_->model.set_status("Battery read unavailable");
        state_->status_message = "Battery read unavailable";
        render();
        return;
    }
    state_->active_generation = state_->requests.generation();
}

void LvSettingBatteryInfoPage3::handle_read_result(int outcome,
                                                   int code,
                                                   std::uint64_t generation,
                                                   const std::string &payload)
{
    if (!state_ || generation != state_->active_generation)
        return;

    if (outcome == static_cast<int>(BatteryOutcome::Success)) {
        if (state_->model.update(code, payload))
            state_->status_message = "Battery updated";
        else
            state_->status_message = state_->model.status_text();
    } else if (outcome == static_cast<int>(BatteryOutcome::TimedOut)) {
        state_->model.set_status("Battery read timed out");
        state_->status_message = "Battery read timed out";
    } else if (outcome == static_cast<int>(BatteryOutcome::Cancelled)) {
        state_->model.set_status("Battery read cancelled");
        state_->status_message = "Battery read cancelled";
    } else {
        state_->model.update(code, payload);
        state_->status_message = state_->model.status_text();
    }
    render();
}

void LvSettingBatteryInfoPage3::render()
{
    if (!state_) return;
    const auto &labels = state_->model.labels();
    for (std::size_t index = 0; index < state_->value_labels.size(); ++index) {
        if (state_->value_labels[index])
            lv_label_set_text(state_->value_labels[index], labels[index].c_str());
    }
    if (state_->status_label)
        lv_label_set_text(state_->status_label, state_->status_message.c_str());
}

void LvSettingBatteryInfoPage3::stop_refresh_timer()
{
    if (!state_ || !state_->refresh_timer) return;
    lv_timer_delete(state_->refresh_timer);
    state_->refresh_timer = nullptr;
}

void LvSettingBatteryInfoPage3::handle_key_event(lv_event_t *event)
{
    if (!event || lv_event_get_code(event) != LV_EVENT_KEY || !state_) return;

    const uint32_t key = lv_event_get_key(event);
    if (key == LV_KEY_ESC || key == LV_KEY_LEFT) {
        state_->requests.cancel();
        if (LeaveSelfPage) LeaveSelfPage();
    } else if (key == LV_KEY_ENTER || key == LV_KEY_RIGHT) {
        request_read();
    }
    lv_event_stop_processing(event);
}

std::unique_ptr<DComponens::LvglComponensBase> settings_battery_info_page_factory(
    lv_obj_t *parent,
    const NodeIter &page_node,
    std::function<void()> on_back)
{
    return std::make_unique<LvSettingBatteryInfoPage3>(
        parent,
        page_node,
        std::move(on_back));
}
