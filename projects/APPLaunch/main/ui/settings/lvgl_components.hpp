#pragma once

#include "lvgl/lvgl.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <future>
#include <functional>
#include <cstring>
#include <cstdlib>
#include <iterator>
#include <list>
#include <memory>
#include <new>
#include <string>
#include <utility>

#include "cp0_font_service.hpp"
#include "cp0_lvgl_app_page_assets.h"
#include "settings_tree_types.hpp"

#define lv_event_get_target(e) (lv_obj_t *)lv_event_get_target(e)

// Generated from https://lvgl.100ask.net/master/examples.html
// Each example is represented as a standalone C++ component class.
namespace DComponens {

// #if !defined(LVGL_COMPONENTS_ROLLER1_ONLY)
struct LvglBindContext {
    std::function<void(lv_event_t *)> fun;
    void *user_data;
    lv_event_code_t event_code;
};

static void lvgl_bind_context_free(void *data)
{
    delete static_cast<LvglBindContext *>(data);
}

static void LvglBindCallback(lv_event_t *event)
{
    LvglBindContext *context = static_cast<LvglBindContext *>(lv_event_get_user_data(event));
    if (!context) return;

#if !LV_USE_EXT_DATA
    const lv_event_code_t event_code = lv_event_get_code(event);
    if (event_code == LV_EVENT_DELETE) {
        if (context->event_code == LV_EVENT_DELETE || context->event_code == LV_EVENT_ALL) {
            if (context->fun) context->fun(event);
        }
        delete context;
        return;
    }
    if (context->event_code != LV_EVENT_ALL && context->event_code != event_code) return;
#endif

    if (!context->fun) return;
    context->fun(event);
}

template <typename Fun>
bool lvgl_bind_event(lv_obj_t *obj, lv_event_code_t event_code, void *user_data, Fun &&fun)
{
    if (!obj) return false;

    LvglBindContext *context = new LvglBindContext{
        std::function<void(lv_event_t *)>(std::forward<Fun>(fun)), user_data, event_code};
#if LV_USE_EXT_DATA
    lv_event_dsc_t *dsc = lv_obj_add_event_cb(obj, LvglBindCallback, event_code, context);
#else
    lv_event_dsc_t *dsc = lv_obj_add_event_cb(obj, LvglBindCallback, LV_EVENT_ALL, context);
#endif
    if (!dsc) {
        delete context;
        return false;
    }
#if LV_USE_EXT_DATA
    lv_event_desc_set_external_data(dsc, context, lvgl_bind_context_free);
#endif
    return true;
}
// #endif

class LvglComponensBase {
public:
    enum class AsyncTaskStage {
        Starting,
        Submitted,
        Waiting,
        Completed,
        TimedOut,
        Failed,
        ScheduleFailed,
    };
    struct AsyncTaskContext {
        using Clock = std::chrono::steady_clock;

        AsyncTaskStage stage           = AsyncTaskStage::Starting;
        Clock::time_point started_at   = Clock::now();
        Clock::time_point last_poll_at = started_at;
        std::size_t poll_count         = 0;
    };

    struct AsyncTaskOptions {
        std::chrono::milliseconds timeout{3000};
    };

    template <typename Result>
    struct AsyncTaskCallbacks {
        std::function<Result()> execute;
        std::function<void(AsyncTaskContext &)> on_start;
        std::function<void(AsyncTaskContext &)> on_submitted;
        std::function<void(AsyncTaskContext &)> on_wait;
        std::function<void(AsyncTaskContext &, const Result &)> on_complete;
        std::function<void(AsyncTaskContext &, std::exception_ptr)> on_exception;
        std::function<void(AsyncTaskContext &)> on_timeout;
        std::function<void(AsyncTaskContext &)> on_schedule_failed;
    };

    using AsyncDispatch = SettingsAsync::Dispatch;
    using AsyncToken = AsyncDispatch::Token;
    using LifetimeToken = AsyncToken;
    using AsyncTaskRegistry = SettingsAsync::SettingsAsyncTaskRegistry;

    lv_obj_t *ComponensObj = nullptr;
    bool NextActive = false;

    lv_obj_t *Get() const
    {
        return ComponensObj;
    }
    void SetPos(int32_t x, int32_t y) const
    {
        lv_obj_set_pos(ComponensObj, x, y);
    }
    void SetSize(int32_t w, int32_t h) const
    {
        lv_obj_set_size(ComponensObj, w, h);
    }
    void OnEvent(lv_event_code_t event_code, std::function<void(lv_event_t *)> &&fun, void *user_data) const
    {
        DComponens::lvgl_bind_event(ComponensObj, event_code, user_data,
                                    std::forward<std::function<void(lv_event_t *)>>(fun));
    }
    void SetWidth(int32_t w) const
    {
        lv_obj_set_width(ComponensObj, w);
    }
    void SetHeight(int32_t h) const
    {
        lv_obj_set_height(ComponensObj, h);
    }

    virtual void AnimateNextIn(std::function<void()> AnimateOverFunc)
    {
        if (AnimateOverFunc) AnimateOverFunc();
    };
    virtual void AnimateNextOut(std::function<void()> AnimateOverFunc)
    {
        if (AnimateOverFunc) AnimateOverFunc();
    };

    virtual void LoadNextPage()  = 0;
    virtual void LeaveNextPage() = 0;

    virtual void SetSelfUiMode(PageType) {}

    std::function<void()> LeaveSelfPage = nullptr;

    LvglComponensBase() = default;
    virtual ~LvglComponensBase()
    {
        cancel_async_tasks();
    }

    bool ensure_async_dispatch()
    {
        if (async_dispatch_timer_) return true;
        if (async_dispatch_unavailable_) return false;

        async_dispatch_timer_ = lv_timer_create(&LvglComponensBase::async_dispatch_timer_cb, 20, this);
        if (!async_dispatch_timer_) {
            async_dispatch_unavailable_ = true;
            async_dispatch_ready_.store(false, std::memory_order_release);
            async_dispatch_.cancel();
            return false;
        }
        async_dispatch_ready_.store(true, std::memory_order_release);
        return true;
    }

    AsyncToken async_token() const noexcept
    {
        return async_dispatch_.token();
    }

    LifetimeToken lifetime_token() const noexcept
    {
        return async_token();
    }

    bool enqueue_async(AsyncToken token, std::function<void()> callback) noexcept
    {
        if (!async_dispatch_ready_.load(std::memory_order_acquire)) return false;
        return AsyncDispatch::enqueue_from_callback(token, std::move(callback));
    }

    bool enqueue_async(std::function<void()> callback) noexcept
    {
        return enqueue_async(async_token(), std::move(callback));
    }

    uint64_t advance_async_generation() noexcept
    {
        return async_dispatch_.advance_generation();
    }

    void cancel_async_tasks() noexcept
    {
        async_dispatch_ready_.store(false, std::memory_order_release);
        async_dispatch_.cancel();
        if (async_dispatch_timer_) {
            lv_timer_delete(async_dispatch_timer_);
            async_dispatch_timer_ = nullptr;
        }
        async_task_registry_.cancel();
        async_task_registry_.join_all();
    }

    void cancel() noexcept
    {
        cancel_async_tasks();
    }

    void reap_finished()
    {
        async_task_registry_.reap_finished();
    }

    void join_all() noexcept
    {
        async_task_registry_.join_all();
    }

    AsyncTaskRegistry &async_tasks() noexcept
    {
        return async_task_registry_;
    }

    template <typename Result>
    bool run_async_task(AsyncTaskCallbacks<Result> callbacks)
    {
        return run_async_task(std::move(callbacks), AsyncTaskOptions{});
    }

    template <typename Result>
    bool run_async_task(AsyncTaskCallbacks<Result> callbacks, AsyncTaskOptions options)
    {
        AsyncTaskContext failed_context;
        if (!callbacks.execute || !ensure_async_dispatch()) {
            failed_context.stage = AsyncTaskStage::ScheduleFailed;
            invoke_async_callback(callbacks.on_schedule_failed, failed_context);
            return false;
        }

        const auto on_schedule_failed = callbacks.on_schedule_failed;
        std::shared_ptr<AsyncTaskState<Result>> state;
        try {
            state = std::make_shared<AsyncTaskState<Result>>(std::move(callbacks), options, async_token());
        } catch (...) {
            failed_context.stage = AsyncTaskStage::ScheduleFailed;
            invoke_async_callback(on_schedule_failed, failed_context);
            return false;
        }
        if (!state->token.valid()) {
            state->context.stage = AsyncTaskStage::ScheduleFailed;
            invoke_async_callback(state->callbacks.on_schedule_failed, state->context);
            return false;
        }

        invoke_async_callback(state->callbacks.on_start, state->context);
        if (!state->token.valid()) return false;

        if (!enqueue_async(state->token, [this, state] { process_async_task(state); })) {
            state->context.stage = AsyncTaskStage::ScheduleFailed;
            invoke_async_callback(state->callbacks.on_schedule_failed, state->context);
            return false;
        }
        return true;
    }

    virtual void create_ui(lv_obj_t *parent) = 0;

private:
    template <typename Result>
    struct AsyncTaskState {
        AsyncTaskCallbacks<Result> callbacks;
        AsyncTaskOptions options;
        AsyncTaskContext context;
        std::promise<Result> promise;
        std::shared_future<Result> future;
        AsyncToken token;
        bool submitted = false;

        AsyncTaskState(AsyncTaskCallbacks<Result> task_callbacks, AsyncTaskOptions task_options,
                       AsyncToken task_token)
            : callbacks(std::move(task_callbacks)), options(task_options), token(std::move(task_token))
        {
        }
    };

    template <typename Callback, typename... Args>
    static void invoke_async_callback(const Callback &callback, Args &&...args) noexcept
    {
        if (!callback) return;
        try {
            callback(std::forward<Args>(args)...);
        } catch (...) {
        }
    }

    static void async_dispatch_timer_cb(lv_timer_t *timer)
    {
        auto *self = timer ? static_cast<LvglComponensBase *>(lv_timer_get_user_data(timer)) : nullptr;
        if (!self) return;
        self->async_task_registry_.reap_finished();
        self->async_dispatch_.drain();
    }

    template <typename Result>
    void schedule_async_task(const std::shared_ptr<AsyncTaskState<Result>> &state)
    {
        if (!state || !state->token.valid()) return;
        if (enqueue_async(state->token, [this, state] { process_async_task(state); })) return;

        state->context.stage = AsyncTaskStage::ScheduleFailed;
        invoke_async_callback(state->callbacks.on_schedule_failed, state->context);
    }

    template <typename Result>
    void process_async_task(const std::shared_ptr<AsyncTaskState<Result>> &state)
    {
        if (!state || !AsyncDispatch::token_is_current(state->token)) return;

        if (!state->submitted) {
            state->submitted            = true;
            state->context.stage        = AsyncTaskStage::Submitted;
            state->context.started_at   = AsyncTaskContext::Clock::now();
            state->context.last_poll_at = state->context.started_at;

            try {
                auto execute = std::move(state->callbacks.execute);
                state->future = state->promise.get_future().share();
                if (!async_task_registry_.start(
                        [state, execute = std::move(execute)]() mutable {
                            try {
                                state->promise.set_value(execute());
                            } catch (...) {
                                try {
                                    state->promise.set_exception(std::current_exception());
                                } catch (...) {
                                }
                            }
                        })) {
                    state->context.stage = AsyncTaskStage::Failed;
                    invoke_async_callback(state->callbacks.on_exception, state->context, std::exception_ptr{});
                    return;
                }
            } catch (...) {
                state->context.stage = AsyncTaskStage::Failed;
                invoke_async_callback(state->callbacks.on_exception, state->context, std::current_exception());
                return;
            }

            invoke_async_callback(state->callbacks.on_submitted, state->context);
            if (!state->token.valid()) return;
            schedule_async_task(state);
            return;
        }

        const auto now   = AsyncTaskContext::Clock::now();
        const bool ready = state->future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready;
        if (!ready && state->options.timeout.count() >= 0 &&
            now - state->context.started_at >= state->options.timeout) {
            state->context.stage = AsyncTaskStage::TimedOut;
            invoke_async_callback(state->callbacks.on_timeout, state->context);
            return;
        }

        if (!ready) {
            state->context.stage        = AsyncTaskStage::Waiting;
            state->context.last_poll_at = now;
            ++state->context.poll_count;
            invoke_async_callback(state->callbacks.on_wait, state->context);
            if (!state->token.valid()) return;
            schedule_async_task(state);
            return;
        }

        try {
            const Result &result = state->future.get();
            state->context.stage = AsyncTaskStage::Completed;
            invoke_async_callback(state->callbacks.on_complete, state->context, result);
        } catch (...) {
            state->context.stage = AsyncTaskStage::Failed;
            invoke_async_callback(state->callbacks.on_exception, state->context, std::current_exception());
        }
    }

    AsyncDispatch async_dispatch_;
    AsyncTaskRegistry async_task_registry_;
    lv_timer_t *async_dispatch_timer_ = nullptr;
    std::atomic_bool async_dispatch_ready_{false};
    bool async_dispatch_unavailable_ = false;
};

class HelloWorldComponens : public LvglComponensBase {
public:
    /**
     * Basic example to create a "Hello world" label
     */
    HelloWorldComponens() = default;
    HelloWorldComponens(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        /*Change the active screen's background color*/
        lv_obj_set_style_bg_color(parent, lv_color_hex(0x003a57), LV_PART_MAIN);

        /*Create a white label, set its text and align it to the center*/
        lv_obj_t *label = lv_label_create(parent);
        lv_label_set_text(label, "Hello world");
        lv_obj_set_style_text_color(parent, lv_color_hex(0xffffff), LV_PART_MAIN);
        lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
    }
};

class ButtonComponens : public LvglComponensBase {
public:
    // static void btn_event_cb(lv_event_t *e)
    // {
    //     lv_obj_t * btn = (lv_obj_t *)lv_event_get_target(e);
    //     static uint8_t cnt = 0;
    //     cnt++;
    //     /*Get the first child of the button which is the label and change its text*/
    //     lv_obj_t * label = lv_obj_get_child(btn, 0);
    //     lv_label_set_text_fmt(label, "Button: %d", cnt);
    // }
    /**
     * Create a button with a label and react on click event.
     */
    ButtonComponens() = default;
    ButtonComponens(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void SetLabelText(const char *text) const
    {
        lv_obj_t *label = lv_obj_get_child(ComponensObj, 0);
        lv_label_set_text(label, text);
    }
    void create_ui(lv_obj_t *parent) override
    {
        lv_obj_t *btn = lv_button_create(parent); /*Add a button the current screen*/
        // lv_obj_set_pos(btn, 10, 10);                            /*Set its position*/
        // lv_obj_set_size(btn, 120, 50);                          /*Set its size*/
        // lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_t *label = lv_label_create(btn); /*Add a label to the button*/
        // lv_label_set_text(label, "Button");                     /*Set the labels text*/
        lv_obj_center(label);
        ComponensObj = btn;
    }
};

#if defined(LVGL_COMPONENTS_ENABLE_EXAMPLES)
class LvExampleGetStarted3 : public LvglComponensBase {
public:
    inline static lv_style_t style_btn;
    inline static lv_style_t style_button_pressed;
    inline static lv_style_t style_button_red;

    static lv_color_t darken(const lv_color_filter_dsc_t *dsc, lv_color_t color, lv_opa_t opa)
    {
        LV_UNUSED(dsc);
        return lv_color_darken(color, opa);
    }

    void style_init(void)
    {
        /*Create a simple button style*/
        lv_style_init(&style_btn);
        lv_style_set_radius(&style_btn, 10);
        lv_style_set_bg_opa(&style_btn, LV_OPA_COVER);
        lv_style_set_bg_color(&style_btn, lv_palette_lighten(LV_PALETTE_GREY, 3));
        lv_style_set_bg_grad_color(&style_btn, lv_palette_main(LV_PALETTE_GREY));
        lv_style_set_bg_grad_dir(&style_btn, LV_GRAD_DIR_VER);

        lv_style_set_border_color(&style_btn, lv_color_black());
        lv_style_set_border_opa(&style_btn, LV_OPA_20);
        lv_style_set_border_width(&style_btn, 2);

        lv_style_set_text_color(&style_btn, lv_color_black());

        /*Create a style for the pressed state.
         *Use a color filter to simply modify all colors in this state*/
        static lv_color_filter_dsc_t color_filter;
        lv_color_filter_dsc_init(&color_filter, darken);
        lv_style_init(&style_button_pressed);
        lv_style_set_color_filter_dsc(&style_button_pressed, &color_filter);
        lv_style_set_color_filter_opa(&style_button_pressed, LV_OPA_20);

        /*Create a red style. Change only some colors.*/
        lv_style_init(&style_button_red);
        lv_style_set_bg_color(&style_button_red, lv_palette_main(LV_PALETTE_RED));
        lv_style_set_bg_grad_color(&style_button_red, lv_palette_lighten(LV_PALETTE_RED, 3));
    }

    /**
     * Create styles from scratch for buttons.
     */
    LvExampleGetStarted3() = default;
    LvExampleGetStarted3(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        /*Initialize the style*/
        style_init();

        /*Create a button and use the new styles*/
        lv_obj_t *btn = lv_button_create(parent);
        /* Remove the styles coming from the theme
         * Note that size and position are also stored as style properties
         * so lv_obj_remove_style_all will remove the set size and position too */
        lv_obj_remove_style_all(btn);
        lv_obj_set_pos(btn, 10, 10);
        lv_obj_set_size(btn, 120, 50);
        lv_obj_add_style(btn, &style_btn, 0);
        lv_obj_add_style(btn, &style_button_pressed, LV_STATE_PRESSED);

        /*Add a label to the button*/
        lv_obj_t *label = lv_label_create(btn);
        lv_label_set_text(label, "Button");
        lv_obj_center(label);

        /*Create another button and use the red style too*/
        lv_obj_t *btn2 = lv_button_create(parent);
        lv_obj_remove_style_all(btn2); /*Remove the styles coming from the theme*/
        lv_obj_set_pos(btn2, 10, 80);
        lv_obj_set_size(btn2, 120, 50);
        lv_obj_add_style(btn2, &style_btn, 0);
        lv_obj_add_style(btn2, &style_button_red, 0);
        lv_obj_add_style(btn2, &style_button_pressed, LV_STATE_PRESSED);
        lv_obj_set_style_radius(btn2, LV_RADIUS_CIRCLE, 0); /*Add a local style too*/

        label = lv_label_create(btn2);
        lv_label_set_text(label, "Button 2");
        lv_obj_center(label);
    }
};

class LvExampleGetStarted4 : public LvglComponensBase {
public:
    lv_obj_t *label;

    void slider_event_cb(lv_event_t *e)
    {
        lv_obj_t *slider = lv_event_get_target(e);

        /*Refresh the text*/
        lv_label_set_text_fmt(label, "%" LV_PRId32, lv_slider_get_value(slider));
        lv_obj_align_to(label, slider, LV_ALIGN_OUT_TOP_MID, 0, -15); /*Align top of the slider*/
    }
    /**
     * Create a slider and write its value on a label.
     */
    LvExampleGetStarted4() = default;
    LvExampleGetStarted4(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        /*Create a slider in the center of the display*/
        lv_obj_t *slider = lv_slider_create(parent);
        lv_obj_set_width(slider, 200); /*Set the width*/
        lv_obj_center(slider);         /*Align to the center of the parent (screen)*/
        DComponens::lvgl_bind_event(slider, LV_EVENT_VALUE_CHANGED, NULL,
                                    std::bind(&LvExampleGetStarted4::slider_event_cb, this, std::placeholders::_1));

        /*Create a label above the slider*/
        label = lv_label_create(parent);
        lv_label_set_text(label, "0");
        lv_obj_align_to(label, slider, LV_ALIGN_OUT_TOP_MID, 0, -15); /*Align top of the slider*/
    }
};

class LvExampleStyle1 : public LvglComponensBase {
public:
    /**
     * Using the Size, Position and Padding style properties
     */
    LvExampleStyle1() = default;
    LvExampleStyle1(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        static lv_style_t style;
        lv_style_init(&style);
        lv_style_set_radius(&style, 5);

        /*Make a gradient*/
        lv_style_set_width(&style, 150);
        lv_style_set_height(&style, LV_SIZE_CONTENT);

        lv_style_set_pad_ver(&style, 20);
        lv_style_set_pad_left(&style, 5);

        lv_style_set_x(&style, lv_pct(50));
        lv_style_set_y(&style, 80);

        /*Create an object with the new style*/
        lv_obj_t *obj = lv_obj_create(parent);
        lv_obj_add_style(obj, &style, 0);

        lv_obj_t *label = lv_label_create(obj);
        lv_label_set_text(label, "Hello");
    }
};

class LvExampleStyle2 : public LvglComponensBase {
public:
    /**
     * Using the background style properties
     */
    LvExampleStyle2() = default;
    LvExampleStyle2(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        static lv_style_t style;
        lv_style_init(&style);
        lv_style_set_radius(&style, 5);

        /*Make a gradient*/
        lv_style_set_bg_opa(&style, LV_OPA_COVER);
        static lv_grad_dsc_t grad;
        grad.dir            = LV_GRAD_DIR_VER;
        grad.stops_count    = 2;
        grad.stops[0].color = lv_palette_lighten(LV_PALETTE_GREY, 1);
        grad.stops[0].opa   = LV_OPA_COVER;
        grad.stops[1].color = lv_palette_main(LV_PALETTE_BLUE);
        grad.stops[1].opa   = LV_OPA_COVER;

        /*Shift the gradient to the bottom*/
        grad.stops[0].frac = 128;
        grad.stops[1].frac = 192;

        lv_style_set_bg_grad(&style, &grad);

        /*Create an object with the new style*/
        lv_obj_t *obj = lv_obj_create(parent);
        lv_obj_add_style(obj, &style, 0);
        lv_obj_center(obj);
    }
};

class LvExampleStyle3 : public LvglComponensBase {
public:
    /**
     * Using the border style properties
     */
    LvExampleStyle3() = default;
    LvExampleStyle3(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        static lv_style_t style;
        lv_style_init(&style);

        /*Set a background color and a radius*/
        lv_style_set_radius(&style, 10);
        lv_style_set_bg_opa(&style, LV_OPA_COVER);
        lv_style_set_bg_color(&style, lv_palette_lighten(LV_PALETTE_GREY, 1));

        /*Add border to the bottom+right*/
        lv_style_set_border_color(&style, lv_palette_main(LV_PALETTE_BLUE));
        lv_style_set_border_width(&style, 5);
        lv_style_set_border_opa(&style, LV_OPA_50);
        lv_style_set_border_side(&style, LV_BORDER_SIDE_BOTTOM | LV_BORDER_SIDE_RIGHT);

        /*Create an object with the new style*/
        lv_obj_t *obj = lv_obj_create(parent);
        lv_obj_add_style(obj, &style, 0);
        lv_obj_center(obj);
    }
};

class LvExampleStyle4 : public LvglComponensBase {
public:
    /**
     * Using the outline style properties
     */
    LvExampleStyle4() = default;
    LvExampleStyle4(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        static lv_style_t style;
        lv_style_init(&style);

        /*Set a background color and a radius*/
        lv_style_set_radius(&style, 5);
        lv_style_set_bg_opa(&style, LV_OPA_COVER);
        lv_style_set_bg_color(&style, lv_palette_lighten(LV_PALETTE_GREY, 1));

        /*Add outline*/
        lv_style_set_outline_width(&style, 2);
        lv_style_set_outline_color(&style, lv_palette_main(LV_PALETTE_BLUE));
        lv_style_set_outline_pad(&style, 8);

        /*Create an object with the new style*/
        lv_obj_t *obj = lv_obj_create(parent);
        lv_obj_add_style(obj, &style, 0);
        lv_obj_center(obj);
    }
};

class LvExampleStyle5 : public LvglComponensBase {
public:
    /**
     * Using the Shadow style properties
     */
    LvExampleStyle5() = default;
    LvExampleStyle5(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        static lv_style_t style;
        lv_style_init(&style);

        /*Set a background color and a radius*/
        lv_style_set_radius(&style, 5);
        lv_style_set_bg_opa(&style, LV_OPA_COVER);
        lv_style_set_bg_color(&style, lv_palette_lighten(LV_PALETTE_GREY, 1));

        /*Add a shadow*/
        lv_style_set_shadow_width(&style, 55);
        lv_style_set_shadow_color(&style, lv_palette_main(LV_PALETTE_BLUE));

        /*Create an object with the new style*/
        lv_obj_t *obj = lv_obj_create(parent);
        lv_obj_add_style(obj, &style, 0);
        lv_obj_center(obj);
    }
};

class LvExampleStyle6 : public LvglComponensBase {
public:
    /**
     * Using the Image style properties
     */
    LvExampleStyle6() = default;
    LvExampleStyle6(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        static lv_style_t style;
        lv_style_init(&style);

        /*Set a background color and a radius*/
        lv_style_set_radius(&style, 5);
        lv_style_set_bg_opa(&style, LV_OPA_COVER);
        lv_style_set_bg_color(&style, lv_palette_lighten(LV_PALETTE_GREY, 3));
        lv_style_set_border_width(&style, 2);
        lv_style_set_border_color(&style, lv_palette_main(LV_PALETTE_BLUE));

        lv_style_set_image_recolor(&style, lv_palette_main(LV_PALETTE_BLUE));
        lv_style_set_image_recolor_opa(&style, LV_OPA_50);
        lv_style_set_transform_rotation(&style, 300);

        /*Create an object with the new style*/
        lv_obj_t *obj = lv_image_create(parent);
        lv_obj_add_style(obj, &style, 0);

        LV_IMAGE_DECLARE(img_cogwheel_argb);
        lv_image_set_src(obj, &img_cogwheel_argb);

        lv_obj_center(obj);
    }
};

class LvExampleStyle7 : public LvglComponensBase {
public:
    /**
     * Using the Arc style properties
     */
    LvExampleStyle7() = default;
    LvExampleStyle7(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        static lv_style_t style;
        lv_style_init(&style);

        lv_style_set_arc_color(&style, lv_palette_main(LV_PALETTE_RED));
        lv_style_set_arc_width(&style, 4);

        /*Create an object with the new style*/
        lv_obj_t *obj = lv_arc_create(parent);
        lv_obj_add_style(obj, &style, 0);
        lv_obj_center(obj);
    }
};

class LvExampleStyle8 : public LvglComponensBase {
public:
    /**
     * Using the text style properties
     */
    LvExampleStyle8() = default;
    LvExampleStyle8(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        static lv_style_t style;
        lv_style_init(&style);

        lv_style_set_radius(&style, 5);
        lv_style_set_bg_opa(&style, LV_OPA_COVER);
        lv_style_set_bg_color(&style, lv_palette_lighten(LV_PALETTE_GREY, 2));
        lv_style_set_border_width(&style, 2);
        lv_style_set_border_color(&style, lv_palette_main(LV_PALETTE_BLUE));
        lv_style_set_pad_all(&style, 10);

        lv_style_set_text_color(&style, lv_palette_main(LV_PALETTE_BLUE));
        lv_style_set_text_letter_space(&style, 5);
        lv_style_set_text_line_space(&style, 20);
        lv_style_set_text_decor(&style, LV_TEXT_DECOR_UNDERLINE);

        /*Create an object with the new style*/
        lv_obj_t *obj = lv_label_create(parent);
        lv_obj_add_style(obj, &style, 0);
        lv_label_set_text(obj,
                          "Text of\n"
                          "a label");

        lv_obj_center(obj);
    }
};

class LvExampleStyle9 : public LvglComponensBase {
public:
    /**
     * Using the line style properties
     */
    LvExampleStyle9() = default;
    LvExampleStyle9(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        static lv_style_t style;
        lv_style_init(&style);

        lv_style_set_line_color(&style, lv_palette_main(LV_PALETTE_GREY));
        lv_style_set_line_width(&style, 6);
        lv_style_set_line_rounded(&style, true);

        /*Create an object with the new style*/
        lv_obj_t *obj = lv_line_create(parent);
        lv_obj_add_style(obj, &style, 0);

        static lv_point_precise_t p[] = {{10, 30}, {30, 50}, {100, 0}};
        lv_line_set_points(obj, p, 3);

        lv_obj_center(obj);
    }
};

class LvExampleStyle10 : public LvglComponensBase {
public:
    /**
     * Creating a transition
     */
    LvExampleStyle10() = default;
    LvExampleStyle10(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        static const lv_style_prop_t props[] = {LV_STYLE_BG_COLOR, LV_STYLE_BORDER_COLOR, LV_STYLE_BORDER_WIDTH, 0};

        /* A default transition
         * Make it fast (100ms) and start with some delay (200 ms)*/
        static lv_style_transition_dsc_t trans_def;
        lv_style_transition_dsc_init(&trans_def, props, lv_anim_path_linear, 100, 200, NULL);

        /* A special transition when going to pressed state
         * Make it slow (500 ms) but start  without delay*/
        static lv_style_transition_dsc_t trans_pr;
        lv_style_transition_dsc_init(&trans_pr, props, lv_anim_path_linear, 500, 0, NULL);

        static lv_style_t style_def;
        lv_style_init(&style_def);
        lv_style_set_transition(&style_def, &trans_def);

        static lv_style_t style_pr;
        lv_style_init(&style_pr);
        lv_style_set_bg_color(&style_pr, lv_palette_main(LV_PALETTE_RED));
        lv_style_set_border_width(&style_pr, 6);
        lv_style_set_border_color(&style_pr, lv_palette_darken(LV_PALETTE_RED, 3));
        lv_style_set_transition(&style_pr, &trans_pr);

        /*Create an object with the new style_pr*/
        lv_obj_t *obj = lv_obj_create(parent);
        lv_obj_add_style(obj, &style_def, 0);
        lv_obj_add_style(obj, &style_pr, LV_STATE_PRESSED);

        lv_obj_center(obj);
    }
};

class LvExampleStyle11 : public LvglComponensBase {
public:
    /**
     * Using multiple styles
     */
    LvExampleStyle11() = default;
    LvExampleStyle11(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        /*A base style*/
        static lv_style_t style_base;
        lv_style_init(&style_base);
        lv_style_set_bg_color(&style_base, lv_palette_main(LV_PALETTE_LIGHT_BLUE));
        lv_style_set_border_color(&style_base, lv_palette_darken(LV_PALETTE_LIGHT_BLUE, 3));
        lv_style_set_border_width(&style_base, 2);
        lv_style_set_radius(&style_base, 10);
        lv_style_set_shadow_width(&style_base, 10);
        lv_style_set_shadow_offset_y(&style_base, 5);
        lv_style_set_shadow_opa(&style_base, LV_OPA_50);
        lv_style_set_text_color(&style_base, lv_color_white());
        lv_style_set_width(&style_base, 100);
        lv_style_set_height(&style_base, LV_SIZE_CONTENT);

        /*Set only the properties that should be different*/
        static lv_style_t style_warning;
        lv_style_init(&style_warning);
        lv_style_set_bg_color(&style_warning, lv_palette_main(LV_PALETTE_YELLOW));
        lv_style_set_border_color(&style_warning, lv_palette_darken(LV_PALETTE_YELLOW, 3));
        lv_style_set_text_color(&style_warning, lv_palette_darken(LV_PALETTE_YELLOW, 4));

        /*Create an object with the base style only*/
        lv_obj_t *obj_base = lv_obj_create(parent);
        lv_obj_add_style(obj_base, &style_base, 0);
        lv_obj_align(obj_base, LV_ALIGN_LEFT_MID, 20, 0);

        lv_obj_t *label = lv_label_create(obj_base);
        lv_label_set_text(label, "Base");
        lv_obj_center(label);

        /*Create another object with the base style and earnings style too*/
        lv_obj_t *obj_warning = lv_obj_create(parent);
        lv_obj_add_style(obj_warning, &style_base, 0);
        lv_obj_add_style(obj_warning, &style_warning, 0);
        lv_obj_align(obj_warning, LV_ALIGN_RIGHT_MID, -20, 0);

        label = lv_label_create(obj_warning);
        lv_label_set_text(label, "Warning");
        lv_obj_center(label);
    }
};

class LvExampleStyle12 : public LvglComponensBase {
public:
    /**
     * Local styles
     */
    LvExampleStyle12() = default;
    LvExampleStyle12(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        static lv_style_t style;
        lv_style_init(&style);
        lv_style_set_bg_color(&style, lv_palette_main(LV_PALETTE_GREEN));
        lv_style_set_border_color(&style, lv_palette_lighten(LV_PALETTE_GREEN, 3));
        lv_style_set_border_width(&style, 3);

        lv_obj_t *obj = lv_obj_create(parent);
        lv_obj_add_style(obj, &style, 0);

        /*Overwrite the background color locally*/
        lv_obj_set_style_bg_color(obj, lv_palette_main(LV_PALETTE_ORANGE), LV_PART_MAIN);

        lv_obj_center(obj);
    }
};

class LvExampleStyle13 : public LvglComponensBase {
public:
    /**
     * Add styles to parts and states
     */
    LvExampleStyle13() = default;
    LvExampleStyle13(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        static lv_style_t style_indic;
        lv_style_init(&style_indic);
        lv_style_set_bg_color(&style_indic, lv_palette_lighten(LV_PALETTE_RED, 3));
        lv_style_set_bg_grad_color(&style_indic, lv_palette_main(LV_PALETTE_RED));
        lv_style_set_bg_grad_dir(&style_indic, LV_GRAD_DIR_HOR);

        static lv_style_t style_indic_pr;
        lv_style_init(&style_indic_pr);
        lv_style_set_shadow_color(&style_indic_pr, lv_palette_main(LV_PALETTE_RED));
        lv_style_set_shadow_width(&style_indic_pr, 10);
        lv_style_set_shadow_spread(&style_indic_pr, 3);

        /*Create an object with the new style_pr*/
        lv_obj_t *obj = lv_slider_create(parent);
        lv_obj_add_style(obj, &style_indic, LV_PART_INDICATOR);
        lv_obj_add_style(obj, &style_indic_pr, LV_PART_INDICATOR | LV_STATE_PRESSED);
        lv_slider_set_value(obj, 70, LV_ANIM_OFF);
        lv_obj_center(obj);
    }
};

class LvExampleStyle14 : public LvglComponensBase {
public:
    inline static lv_style_t style_btn;

    /*Will be called when the styles of the base theme are already added
      to add new styles*/
    static void new_theme_apply_cb(lv_theme_t *th, lv_obj_t *obj)
    {
        LV_UNUSED(th);

        if (lv_obj_check_type(obj, &lv_button_class)) {
            lv_obj_add_style(obj, &style_btn, 0);
        }
    }

    static void new_theme_init_and_set(void)
    {
        /*Initialize the styles*/
        lv_style_init(&style_btn);
        lv_style_set_bg_color(&style_btn, lv_palette_main(LV_PALETTE_GREEN));
        lv_style_set_border_color(&style_btn, lv_palette_darken(LV_PALETTE_GREEN, 3));
        lv_style_set_border_width(&style_btn, 3);

        /*Initialize the new theme from the current theme*/
        lv_theme_t *th_act = lv_display_get_theme(NULL);
        static lv_theme_t th_new;
        th_new = *th_act;

        /*Set the parent theme and the style apply callback for the new theme*/
        lv_theme_set_parent(&th_new, th_act);
        lv_theme_set_apply_cb(&th_new, new_theme_apply_cb);

        /*Assign the new theme to the current display*/
        lv_display_set_theme(NULL, &th_new);
    }

    /**
     * Extending the current theme
     */
    LvExampleStyle14() = default;
    LvExampleStyle14(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        lv_obj_t *btn;
        lv_obj_t *label;

        btn = lv_button_create(parent);
        lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, 20);

        label = lv_label_create(btn);
        lv_label_set_text(label, "Original theme");

        new_theme_init_and_set();

        btn = lv_button_create(parent);
        lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -20);

        label = lv_label_create(btn);
        lv_label_set_text(label, "New theme");
    }
};

class LvExampleStyle15 : public LvglComponensBase {
public:
    /**
     * Opacity and Transformations
     */
    LvExampleStyle15() = default;
    LvExampleStyle15(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        lv_obj_t *btn;
        lv_obj_t *label;

        /*Normal button*/
        btn = lv_button_create(parent);
        lv_obj_set_size(btn, 100, 40);
        lv_obj_align(btn, LV_ALIGN_CENTER, 0, -70);

        label = lv_label_create(btn);
        lv_label_set_text(label, "Normal");
        lv_obj_center(label);

        /*Set opacity
         *The button and the label is rendered to a layer first and that layer is blended*/
        btn = lv_button_create(parent);
        lv_obj_set_size(btn, 100, 40);
        lv_obj_set_style_opa(btn, LV_OPA_50, 0);
        lv_obj_align(btn, LV_ALIGN_CENTER, 0, 0);

        label = lv_label_create(btn);
        lv_label_set_text(label, "Opa:50%");
        lv_obj_center(label);

        /*Set transformations
         *The button and the label is rendered to a layer first and that layer is transformed*/
        btn = lv_button_create(parent);
        lv_obj_set_size(btn, 100, 40);
        lv_obj_set_style_transform_rotation(btn, 150, 0);   /*15 deg*/
        lv_obj_set_style_transform_scale(btn, 256 + 64, 0); /*1.25x*/
        lv_obj_set_style_transform_pivot_x(btn, 50, 0);
        lv_obj_set_style_transform_pivot_y(btn, 20, 0);
        lv_obj_set_style_opa(btn, LV_OPA_50, 0);
        lv_obj_align(btn, LV_ALIGN_CENTER, 0, 70);

        label = lv_label_create(btn);
        lv_label_set_text(label, "Transf.");
        lv_obj_center(label);
    }
};

class LvExampleStyle16 : public LvglComponensBase {
public:
#if LV_USE_DRAW_SW_COMPLEX_GRADIENTS

    /**
     * Simulate metallic knob using conical gradient
     * For best effect set LV_GRADIENT_MAX_STOPS to 8 or at least 3
     */
    LvExampleStyle16() = default;
    LvExampleStyle16(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
#if LV_GRADIENT_MAX_STOPS >= 8
        static const lv_color_t grad_colors[8] = {
            LV_COLOR_MAKE(0xe8, 0xe8, 0xe8), LV_COLOR_MAKE(0xff, 0xff, 0xff), LV_COLOR_MAKE(0xfa, 0xfa, 0xfa),
            LV_COLOR_MAKE(0x79, 0x79, 0x79), LV_COLOR_MAKE(0x48, 0x48, 0x48), LV_COLOR_MAKE(0x4b, 0x4b, 0x4b),
            LV_COLOR_MAKE(0x70, 0x70, 0x70), LV_COLOR_MAKE(0xe8, 0xe8, 0xe8),
        };
#elif LV_GRADIENT_MAX_STOPS >= 3
        static const lv_color_t grad_colors[3] = {
            LV_COLOR_MAKE(0xe8, 0xe8, 0xe8),
            LV_COLOR_MAKE(0xff, 0xff, 0xff),
            LV_COLOR_MAKE(0x79, 0x79, 0x79),
        };
#else
        static const lv_color_t grad_colors[2] = {
            LV_COLOR_MAKE(0xe8, 0xe8, 0xe8),
            LV_COLOR_MAKE(0x79, 0x79, 0x79),
        };
#endif

        /*Create a style with gradient background and shadow*/
        static lv_style_t style;
        lv_style_init(&style);
        lv_style_set_radius(&style, 500);
        lv_style_set_bg_opa(&style, LV_OPA_COVER);
        lv_style_set_shadow_color(&style, lv_color_black());
        lv_style_set_shadow_width(&style, 50);
        lv_style_set_shadow_offset_x(&style, 20);
        lv_style_set_shadow_offset_y(&style, 20);
        lv_style_set_shadow_opa(&style, LV_OPA_50);

        /*First define a color gradient. In this example we use a gray color map with random values.*/
        static lv_grad_dsc_t grad;

        lv_gradient_init_stops(&grad, grad_colors, NULL, NULL, sizeof(grad_colors) / sizeof(lv_color_t));

        /*Make a conical gradient with the center in the middle of the object*/
#if LV_GRADIENT_MAX_STOPS >= 8
        lv_grad_conical_init(&grad, LV_GRAD_CENTER, LV_GRAD_CENTER, 0, 120, LV_GRAD_EXTEND_REFLECT);
#elif LV_GRADIENT_MAX_STOPS >= 3
        lv_grad_conical_init(&grad, LV_GRAD_CENTER, LV_GRAD_CENTER, 45, 125, LV_GRAD_EXTEND_REFLECT);
#else
        lv_grad_conical_init(&grad, LV_GRAD_CENTER, LV_GRAD_CENTER, 45, 110, LV_GRAD_EXTEND_REFLECT);
#endif

        /*Set gradient as background*/
        lv_style_set_bg_grad(&style, &grad);

        /*Create an object with the new style*/
        lv_obj_t *obj = lv_obj_create(parent);
        lv_obj_add_style(obj, &style, 0);
        lv_obj_set_size(obj, 200, 200);
        lv_obj_center(obj);
    }

#else

    void lv_example_style_16(void)
    {
        lv_obj_t *label = lv_label_create(lv_screen_active());
        lv_obj_set_width(label, LV_PCT(80));
        lv_label_set_text(label, "LV_USE_DRAW_SW_COMPLEX_GRADIENTS is not enabled");
        lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_center(label);
    }

#endif /*LV_USE_DRAW_SW_COMPLEX_GRADIENTS*/
};

class LvExampleStyle17 : public LvglComponensBase {
public:
#if LV_USE_DRAW_SW_COMPLEX_GRADIENTS

    /**
     * Using radial gradient as background
     */
    LvExampleStyle17() = default;
    LvExampleStyle17(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        static const lv_color_t grad_colors[2] = {
            LV_COLOR_MAKE(0x9B, 0x18, 0x42),
            LV_COLOR_MAKE(0x00, 0x00, 0x00),
        };

        int32_t width  = lv_display_get_horizontal_resolution(NULL);
        int32_t height = lv_display_get_vertical_resolution(NULL);

        static lv_style_t style;
        lv_style_init(&style);

        /*First define a color gradient. In this example we use a purple to black color map.*/
        static lv_grad_dsc_t grad;

        lv_gradient_init_stops(&grad, grad_colors, NULL, NULL, sizeof(grad_colors) / sizeof(lv_color_t));

        /*Make a radial gradient with the center in the middle of the object, extending to the farthest corner*/
        lv_grad_radial_init(&grad, LV_GRAD_CENTER, LV_GRAD_CENTER, LV_GRAD_RIGHT, LV_GRAD_BOTTOM, LV_GRAD_EXTEND_PAD);

        /*Set gradient as background*/
        lv_style_set_bg_grad(&style, &grad);

        /*Create an object with the new style*/
        lv_obj_t *obj = lv_obj_create(parent);
        lv_obj_add_style(obj, &style, 0);
        lv_obj_set_size(obj, width, height);
        lv_obj_center(obj);
    }

#else

    void lv_example_style_17(void)
    {
        lv_obj_t *label = lv_label_create(lv_screen_active());
        lv_obj_set_width(label, LV_PCT(80));
        lv_label_set_text(label, "LV_USE_DRAW_SW_COMPLEX_GRADIENTS is not enabled");
        lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_center(label);
    }

#endif /*LV_USE_DRAW_SW_COMPLEX_GRADIENTS*/
};

class LvExampleStyle18 : public LvglComponensBase {
public:
#if LV_USE_DRAW_SW_COMPLEX_GRADIENTS

    /**
     * Using various gradients for button background
     */
    LvExampleStyle18() = default;
    LvExampleStyle18(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        static const lv_color_t grad_colors[2] = {
            LV_COLOR_MAKE(0x26, 0xa0, 0xda),
            LV_COLOR_MAKE(0x31, 0x47, 0x55),
        };

        /*Create a linear gradient going from the top left corner to the bottom at an angle, with reflected color map*/
        static lv_style_t style_with_linear_gradient_bg;
        static lv_grad_dsc_t linear_gradient_dsc; /*NOTE: the gradient descriptor must be static or global variable!*/

        lv_style_init(&style_with_linear_gradient_bg);
        lv_gradient_init_stops(&linear_gradient_dsc, grad_colors, NULL, NULL, sizeof(grad_colors) / sizeof(lv_color_t));
        lv_grad_linear_init(&linear_gradient_dsc, lv_pct(0), lv_pct(0), lv_pct(20), lv_pct(100),
                            LV_GRAD_EXTEND_REFLECT);
        lv_style_set_bg_grad(&style_with_linear_gradient_bg, &linear_gradient_dsc);
        lv_style_set_bg_opa(&style_with_linear_gradient_bg, LV_OPA_COVER);

        /*Create a radial gradient with the center in the top left 1/3rd of the object, extending to the bottom right
         * corner, with reflected color map*/
        static lv_style_t style_with_radial_gradient_bg;
        static lv_grad_dsc_t radial_gradient_dsc; /*NOTE: the gradient descriptor must be static or global variable!*/

        lv_style_init(&style_with_radial_gradient_bg);
        lv_gradient_init_stops(&radial_gradient_dsc, grad_colors, NULL, NULL, sizeof(grad_colors) / sizeof(lv_color_t));
        lv_grad_radial_init(&radial_gradient_dsc, lv_pct(30), lv_pct(30), lv_pct(100), lv_pct(100),
                            LV_GRAD_EXTEND_REFLECT);
        lv_style_set_bg_grad(&style_with_radial_gradient_bg, &radial_gradient_dsc);
        lv_style_set_bg_opa(&style_with_radial_gradient_bg, LV_OPA_COVER);

        /*Create buttons with different gradient styles*/

        lv_obj_t *btn;
        lv_obj_t *label;

        /*Simple horizontal gradient*/
        btn = lv_button_create(parent);
        lv_obj_set_style_bg_color(btn, grad_colors[0], 0);
        lv_obj_set_style_bg_grad_color(btn, grad_colors[1], 0);
        lv_obj_set_style_bg_grad_dir(btn, LV_GRAD_DIR_HOR, 0);
        lv_obj_set_size(btn, 150, 50);
        lv_obj_align(btn, LV_ALIGN_CENTER, 0, -100);

        label = lv_label_create(btn);
        lv_label_set_text(label, "Horizontal");
        lv_obj_center(label);

        /*Simple vertical gradient*/
        btn = lv_button_create(parent);
        lv_obj_set_style_bg_color(btn, grad_colors[0], 0);
        lv_obj_set_style_bg_grad_color(btn, grad_colors[1], 0);
        lv_obj_set_style_bg_grad_dir(btn, LV_GRAD_DIR_VER, 0);
        lv_obj_set_size(btn, 150, 50);
        lv_obj_align(btn, LV_ALIGN_CENTER, 0, -40);

        label = lv_label_create(btn);
        lv_label_set_text(label, "Vertical");
        lv_obj_center(label);

        /*Complex linear gradient*/
        btn = lv_button_create(parent);
        lv_obj_add_style(btn, &style_with_linear_gradient_bg, 0);
        lv_obj_set_size(btn, 150, 50);
        lv_obj_align(btn, LV_ALIGN_CENTER, 0, 20);

        label = lv_label_create(btn);
        lv_label_set_text(label, "Linear");
        lv_obj_center(label);

        /*Complex radial gradient*/
        btn = lv_button_create(parent);
        lv_obj_add_style(btn, &style_with_radial_gradient_bg, 0);
        lv_obj_set_size(btn, 150, 50);
        lv_obj_align(btn, LV_ALIGN_CENTER, 0, 80);

        label = lv_label_create(btn);
        lv_label_set_text(label, "Radial");
        lv_obj_center(label);
    }

#else

    void lv_example_style_18(void)
    {
        lv_obj_t *label = lv_label_create(lv_screen_active());
        lv_obj_set_width(label, LV_PCT(80));
        lv_label_set_text(label, "LV_USE_DRAW_SW_COMPLEX_GRADIENTS is not enabled");
        lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_center(label);
    }

#endif /*LV_USE_DRAW_SW_COMPLEX_GRADIENTS*/
};

class LvExampleAnim1 : public LvglComponensBase {
public:
    static void anim_x_cb(void *var, int32_t v)
    {
        lv_obj_set_x(var, v);
    }

    static void sw_event_cb(lv_event_t *e)
    {
        lv_obj_t *sw    = lv_event_get_target(e);
        lv_obj_t *label = lv_event_get_user_data(e);

        if (lv_obj_has_state(sw, LV_STATE_CHECKED)) {
            lv_anim_t a;
            lv_anim_init(&a);
            lv_anim_set_var(&a, label);
            lv_anim_set_values(&a, lv_obj_get_x(label), 100);
            lv_anim_set_duration(&a, 500);
            lv_anim_set_exec_cb(&a, anim_x_cb);
            lv_anim_set_path_cb(&a, lv_anim_path_overshoot);
            lv_anim_start(&a);
        } else {
            lv_anim_t a;
            lv_anim_init(&a);
            lv_anim_set_var(&a, label);
            lv_anim_set_values(&a, lv_obj_get_x(label), -lv_obj_get_width(label));
            lv_anim_set_duration(&a, 500);
            lv_anim_set_exec_cb(&a, anim_x_cb);
            lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
            lv_anim_start(&a);
        }
    }

    /**
     * Start animation on an event
     */
    LvExampleAnim1() = default;
    LvExampleAnim1(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        lv_obj_t *label = lv_label_create(parent);
        lv_label_set_text(label, "Hello animations!");
        lv_obj_set_pos(label, 100, 10);

        lv_obj_t *sw = lv_switch_create(parent);
        lv_obj_center(sw);
        lv_obj_add_state(sw, LV_STATE_CHECKED);
        lv_obj_add_event_cb(sw, sw_event_cb, LV_EVENT_VALUE_CHANGED, label);
    }
};

class LvExampleAnim2 : public LvglComponensBase {
public:
    static void anim_x_cb(void *var, int32_t v)
    {
        lv_obj_set_x(var, v);
    }

    static void anim_size_cb(void *var, int32_t v)
    {
        lv_obj_set_size(var, v, v);
    }

    /**
     * Create a playback animation
     */
    LvExampleAnim2() = default;
    LvExampleAnim2(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        lv_obj_t *obj = lv_obj_create(parent);
        lv_obj_set_style_bg_color(obj, lv_palette_main(LV_PALETTE_RED), 0);
        lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, 0);

        lv_obj_align(obj, LV_ALIGN_LEFT_MID, 10, 0);

        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, obj);
        lv_anim_set_values(&a, 10, 50);
        lv_anim_set_duration(&a, 1000);
        lv_anim_set_playback_delay(&a, 100);
        lv_anim_set_playback_duration(&a, 300);
        lv_anim_set_repeat_delay(&a, 500);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);

        lv_anim_set_exec_cb(&a, anim_size_cb);
        lv_anim_start(&a);
        lv_anim_set_exec_cb(&a, anim_x_cb);
        lv_anim_set_values(&a, 10, 240);
        lv_anim_start(&a);
    }
};

class LvExampleAnim3 : public LvglComponensBase {
public:
    /**
     * the example show the use of cubic-bezier3 in animation.
     * the control point P1,P2 of cubic-bezier3 can be adjusted by slider.
     * and the chart shows the cubic-bezier3 in real time. you can click
     * run button see animation in current point of cubic-bezier3.
     */

#define CHART_POINTS_NUM 256

    struct {
        lv_obj_t *anim_obj;
        lv_obj_t *chart;
        lv_chart_series_t *ser1;
        lv_obj_t *p1_slider;
        lv_obj_t *p1_label;
        lv_obj_t *p2_slider;
        lv_obj_t *p2_label;
        lv_obj_t *run_btn;
        uint16_t p1;
        uint16_t p2;
        lv_anim_t a;
    } ginfo;

    inline static int32_t anim_path_bezier3_cb(const lv_anim_t *a);
    static void refer_chart_cubic_bezier(void);
    static void run_button_event_handler(lv_event_t *e);
    static void slider_event_cb(lv_event_t *e);
    static void page_obj_init(lv_obj_t *par);
    static void anim_x_cb(void *var, int32_t v);

    /**
     * create an animation
     */
    LvExampleAnim3() = default;
    LvExampleAnim3(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        static int32_t col_dsc[] = {LV_GRID_FR(1), 200, LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
        static int32_t row_dsc[] = {30, 10, 10, LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};

        /*Create a container with grid*/
        lv_obj_t *cont = lv_obj_create(parent);
        lv_obj_set_style_pad_all(cont, 2, LV_PART_MAIN);
        lv_obj_set_style_pad_column(cont, 10, LV_PART_MAIN);
        lv_obj_set_style_pad_row(cont, 10, LV_PART_MAIN);
        lv_obj_set_grid_dsc_array(cont, col_dsc, row_dsc);
        lv_obj_set_size(cont, 320, 240);
        lv_obj_center(cont);

        page_obj_init(cont);

        lv_anim_init(&ginfo.a);
        lv_anim_set_var(&ginfo.a, ginfo.anim_obj);
        int32_t end =
            lv_obj_get_style_width(cont, LV_PART_MAIN) - lv_obj_get_style_width(ginfo.anim_obj, LV_PART_MAIN) - 10;
        lv_anim_set_values(&ginfo.a, 5, end);
        lv_anim_set_duration(&ginfo.a, 2000);
        lv_anim_set_exec_cb(&ginfo.a, anim_x_cb);
        lv_anim_set_path_cb(&ginfo.a, anim_path_bezier3_cb);

        refer_chart_cubic_bezier();
    }

    inline static int32_t anim_path_bezier3_cb(const lv_anim_t *a)
    {
        uint32_t t   = lv_map(a->act_time, 0, a->duration, 0, 1024);
        int32_t step = lv_bezier3(t, 0, ginfo.p1, ginfo.p2, 1024);
        int32_t new_value;
        new_value = step * (a->end_value - a->start_value);
        new_value = new_value >> 10;
        new_value += a->start_value;
        return new_value;
    }

    static void refer_chart_cubic_bezier(void)
    {
        for (uint16_t i = 0; i <= CHART_POINTS_NUM; i++) {
            uint32_t t   = i * (1024 / CHART_POINTS_NUM);
            int32_t step = lv_bezier3(t, 0, ginfo.p1, ginfo.p2, 1024);
            lv_chart_set_value_by_id2(ginfo.chart, ginfo.ser1, i, t, step);
        }
        lv_chart_refresh(ginfo.chart);
    }

    static void anim_x_cb(void *var, int32_t v)
    {
        lv_obj_set_style_translate_x(var, v, LV_PART_MAIN);
    }

    static void run_button_event_handler(lv_event_t *e)
    {
        lv_event_code_t code = lv_event_get_code(e);
        if (code == LV_EVENT_CLICKED) {
            lv_anim_start(&ginfo.a);
        }
    }

    static void slider_event_cb(lv_event_t *e)
    {
        char buf[16];
        lv_obj_t *label;
        lv_obj_t *slider = lv_event_get_target(e);

        if (slider == ginfo.p1_slider) {
            label    = ginfo.p1_label;
            ginfo.p1 = lv_slider_get_value(slider);
            lv_snprintf(buf, sizeof(buf), "p1:%d", ginfo.p1);
        } else {
            label    = ginfo.p2_label;
            ginfo.p2 = lv_slider_get_value(slider);
            lv_snprintf(buf, sizeof(buf), "p2:%d", ginfo.p2);
        }

        lv_label_set_text(label, buf);
        refer_chart_cubic_bezier();
    }

    static void page_obj_init(lv_obj_t *par)
    {
        ginfo.anim_obj = lv_obj_create(par);
        lv_obj_set_size(ginfo.anim_obj, 30, 30);
        lv_obj_set_align(ginfo.anim_obj, LV_ALIGN_TOP_LEFT);
        lv_obj_remove_flag(ginfo.anim_obj, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(ginfo.anim_obj, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN);
        lv_obj_set_grid_cell(ginfo.anim_obj, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_START, 0, 1);

        ginfo.p1_label = lv_label_create(par);
        ginfo.p2_label = lv_label_create(par);
        lv_label_set_text(ginfo.p1_label, "p1:0");
        lv_label_set_text(ginfo.p2_label, "p2:0");
        lv_obj_set_grid_cell(ginfo.p1_label, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_START, 1, 1);
        lv_obj_set_grid_cell(ginfo.p2_label, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_START, 2, 1);

        ginfo.p1_slider = lv_slider_create(par);
        ginfo.p2_slider = lv_slider_create(par);
        lv_slider_set_range(ginfo.p1_slider, 0, 1024);
        lv_slider_set_range(ginfo.p2_slider, 0, 1024);
        lv_obj_set_style_pad_all(ginfo.p1_slider, 2, LV_PART_KNOB);
        lv_obj_set_style_pad_all(ginfo.p2_slider, 2, LV_PART_KNOB);
        lv_obj_add_event_cb(ginfo.p1_slider, slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
        lv_obj_add_event_cb(ginfo.p2_slider, slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
        lv_obj_set_grid_cell(ginfo.p1_slider, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_START, 1, 1);
        lv_obj_set_grid_cell(ginfo.p2_slider, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_START, 2, 1);

        ginfo.run_btn = lv_button_create(par);
        lv_obj_add_event_cb(ginfo.run_btn, run_button_event_handler, LV_EVENT_CLICKED, NULL);
        lv_obj_t *btn_label = lv_label_create(ginfo.run_btn);
        lv_label_set_text(btn_label, LV_SYMBOL_PLAY);
        lv_obj_center(btn_label);
        lv_obj_set_grid_cell(ginfo.run_btn, LV_GRID_ALIGN_STRETCH, 2, 1, LV_GRID_ALIGN_STRETCH, 1, 2);

        ginfo.chart = lv_chart_create(par);
        lv_obj_set_style_pad_all(ginfo.chart, 0, LV_PART_MAIN);
        lv_obj_set_style_size(ginfo.chart, 0, 0, LV_PART_INDICATOR);
        lv_chart_set_type(ginfo.chart, LV_CHART_TYPE_SCATTER);
        ginfo.ser1 = lv_chart_add_series(ginfo.chart, lv_palette_main(LV_PALETTE_RED), LV_CHART_AXIS_PRIMARY_Y);
        lv_chart_set_range(ginfo.chart, LV_CHART_AXIS_PRIMARY_Y, 0, 1024);
        lv_chart_set_range(ginfo.chart, LV_CHART_AXIS_PRIMARY_X, 0, 1024);
        lv_chart_set_point_count(ginfo.chart, CHART_POINTS_NUM);
        lv_obj_set_grid_cell(ginfo.chart, LV_GRID_ALIGN_STRETCH, 0, 3, LV_GRID_ALIGN_STRETCH, 3, 1);
    }
};

class LvExampleAnimTimeline1 : public LvglComponensBase {
public:
    inline static const int32_t obj_width  = 90;
    inline static const int32_t obj_height = 70;

    static void set_width(lv_anim_t *var, int32_t v)
    {
        lv_obj_set_width(var->var, v);
    }

    static void set_height(lv_anim_t *var, int32_t v)
    {
        lv_obj_set_height(var->var, v);
    }

    static void set_slider_value(lv_anim_t *var, int32_t v)
    {
        lv_slider_set_value(var->var, v, LV_ANIM_OFF);
    }

    static void btn_start_event_handler(lv_event_t *e)
    {
        lv_obj_t *btn                     = lv_event_get_current_target_obj(e);
        lv_anim_timeline_t *anim_timeline = lv_event_get_user_data(e);

        bool reverse = lv_obj_has_state(btn, LV_STATE_CHECKED);
        lv_anim_timeline_set_reverse(anim_timeline, reverse);
        lv_anim_timeline_start(anim_timeline);
    }

    static void btn_pause_event_handler(lv_event_t *e)
    {
        lv_anim_timeline_t *anim_timeline = lv_event_get_user_data(e);
        lv_anim_timeline_pause(anim_timeline);
    }

    static void slider_prg_event_handler(lv_event_t *e)
    {
        lv_obj_t *slider                  = lv_event_get_current_target_obj(e);
        lv_anim_timeline_t *anim_timeline = lv_event_get_user_data(e);
        int32_t progress                  = lv_slider_get_value(slider);
        lv_anim_timeline_set_progress(anim_timeline, progress);
    }

    /**
     * Create an animation timeline
     */
    LvExampleAnimTimeline1() = default;
    LvExampleAnimTimeline1(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        /* Create anim timeline */
        lv_anim_timeline_t *anim_timeline = lv_anim_timeline_create();

        lv_obj_t *par = parent;
        lv_obj_set_flex_flow(par, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(par, LV_FLEX_ALIGN_SPACE_AROUND, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        /* create btn_start */
        lv_obj_t *btn_start = lv_button_create(par);
        lv_obj_add_event_cb(btn_start, btn_start_event_handler, LV_EVENT_VALUE_CHANGED, anim_timeline);
        lv_obj_add_flag(btn_start, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_add_flag(btn_start, LV_OBJ_FLAG_CHECKABLE);
        lv_obj_align(btn_start, LV_ALIGN_TOP_MID, -100, 20);

        lv_obj_t *label_start = lv_label_create(btn_start);
        lv_label_set_text(label_start, "Start");
        lv_obj_center(label_start);

        /* create btn_pause */
        lv_obj_t *btn_pause = lv_button_create(par);
        lv_obj_add_event_cb(btn_pause, btn_pause_event_handler, LV_EVENT_CLICKED, anim_timeline);
        lv_obj_add_flag(btn_pause, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_align(btn_pause, LV_ALIGN_TOP_MID, 100, 20);

        lv_obj_t *label_pause = lv_label_create(btn_pause);
        lv_label_set_text(label_pause, "Pause");
        lv_obj_center(label_pause);

        /* create slider_prg */
        lv_obj_t *slider_prg = lv_slider_create(par);
        lv_obj_add_event_cb(slider_prg, slider_prg_event_handler, LV_EVENT_VALUE_CHANGED, anim_timeline);
        lv_obj_add_flag(slider_prg, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_align(slider_prg, LV_ALIGN_BOTTOM_MID, 0, -20);
        lv_slider_set_range(slider_prg, 0, LV_ANIM_TIMELINE_PROGRESS_MAX);

        /* create 3 objects */
        lv_obj_t *obj1 = lv_obj_create(par);
        lv_obj_set_size(obj1, obj_width, obj_height);
        lv_obj_set_scrollbar_mode(obj1, LV_SCROLLBAR_MODE_OFF);

        lv_obj_t *obj2 = lv_obj_create(par);
        lv_obj_set_size(obj2, obj_width, obj_height);
        lv_obj_set_scrollbar_mode(obj2, LV_SCROLLBAR_MODE_OFF);

        lv_obj_t *obj3 = lv_obj_create(par);
        lv_obj_set_size(obj3, obj_width, obj_height);
        lv_obj_set_scrollbar_mode(obj3, LV_SCROLLBAR_MODE_OFF);

        /* anim-slider */
        lv_anim_t a_slider;
        lv_anim_init(&a_slider);
        lv_anim_set_var(&a_slider, slider_prg);
        lv_anim_set_values(&a_slider, 0, LV_ANIM_TIMELINE_PROGRESS_MAX);
        lv_anim_set_custom_exec_cb(&a_slider, set_slider_value);
        lv_anim_set_path_cb(&a_slider, lv_anim_path_linear);
        lv_anim_set_duration(&a_slider, 700);

        /* anim-obj1 */
        lv_anim_t a1;
        lv_anim_init(&a1);
        lv_anim_set_var(&a1, obj1);
        lv_anim_set_values(&a1, 0, obj_width);
        lv_anim_set_custom_exec_cb(&a1, set_width);
        lv_anim_set_path_cb(&a1, lv_anim_path_overshoot);
        lv_anim_set_duration(&a1, 300);

        lv_anim_t a2;
        lv_anim_init(&a2);
        lv_anim_set_var(&a2, obj1);
        lv_anim_set_values(&a2, 0, obj_height);
        lv_anim_set_custom_exec_cb(&a2, set_height);
        lv_anim_set_path_cb(&a2, lv_anim_path_ease_out);
        lv_anim_set_duration(&a2, 300);

        /* anim-obj2 */
        lv_anim_t a3;
        lv_anim_init(&a3);
        lv_anim_set_var(&a3, obj2);
        lv_anim_set_values(&a3, 0, obj_width);
        lv_anim_set_custom_exec_cb(&a3, set_width);
        lv_anim_set_path_cb(&a3, lv_anim_path_overshoot);
        lv_anim_set_duration(&a3, 300);

        lv_anim_t a4;
        lv_anim_init(&a4);
        lv_anim_set_var(&a4, obj2);
        lv_anim_set_values(&a4, 0, obj_height);
        lv_anim_set_custom_exec_cb(&a4, set_height);
        lv_anim_set_path_cb(&a4, lv_anim_path_ease_out);
        lv_anim_set_duration(&a4, 300);

        /* anim-obj3 */
        lv_anim_t a5;
        lv_anim_init(&a5);
        lv_anim_set_var(&a5, obj3);
        lv_anim_set_values(&a5, 0, obj_width);
        lv_anim_set_custom_exec_cb(&a5, set_width);
        lv_anim_set_path_cb(&a5, lv_anim_path_overshoot);
        lv_anim_set_duration(&a5, 300);

        lv_anim_t a6;
        lv_anim_init(&a6);
        lv_anim_set_var(&a6, obj3);
        lv_anim_set_values(&a6, 0, obj_height);
        lv_anim_set_custom_exec_cb(&a6, set_height);
        lv_anim_set_path_cb(&a6, lv_anim_path_ease_out);
        lv_anim_set_duration(&a6, 300);

        /* add animations to timeline */
        lv_anim_timeline_add(anim_timeline, 0, &a_slider);
        lv_anim_timeline_add(anim_timeline, 0, &a1);
        lv_anim_timeline_add(anim_timeline, 0, &a2);
        lv_anim_timeline_add(anim_timeline, 200, &a3);
        lv_anim_timeline_add(anim_timeline, 200, &a4);
        lv_anim_timeline_add(anim_timeline, 400, &a5);
        lv_anim_timeline_add(anim_timeline, 400, &a6);

        lv_anim_timeline_set_progress(anim_timeline, LV_ANIM_TIMELINE_PROGRESS_MAX);
    }
};

class LvExampleEventClick : public LvglComponensBase {
public:
    static void event_cb(lv_event_t *e)
    {
        LV_LOG_USER("Clicked");

        static uint32_t cnt = 1;
        lv_obj_t *btn       = lv_event_get_target(e);
        lv_obj_t *label     = lv_obj_get_child(btn, 0);
        lv_label_set_text_fmt(label, "%" LV_PRIu32, cnt);
        cnt++;
    }

    /**
     * Add click event to a button
     */
    LvExampleEventClick() = default;
    LvExampleEventClick(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        lv_obj_t *btn = lv_button_create(parent);
        lv_obj_set_size(btn, 100, 50);
        lv_obj_center(btn);
        lv_obj_add_event_cb(btn, event_cb, LV_EVENT_CLICKED, NULL);

        lv_obj_t *label = lv_label_create(btn);
        lv_label_set_text(label, "Click me!");
        lv_obj_center(label);
    }
};

class LvExampleEventStreak : public LvglComponensBase {
public:
    static void short_click_event_cb(lv_event_t *e)
    {
        LV_LOG_USER("Short clicked");

        lv_obj_t *info_label = lv_event_get_user_data(e);
        lv_indev_t *indev    = lv_event_get_param(e);
        uint8_t cnt          = lv_indev_get_short_click_streak(indev);
        lv_label_set_text_fmt(info_label, "Short click streak: %u", cnt);
    }

    static void streak_event_cb(lv_event_t *e)
    {
        lv_obj_t *btn    = lv_event_get_target(e);
        lv_obj_t *label  = lv_obj_get_child(btn, 0);
        const char *text = lv_event_get_user_data(e);
        lv_label_set_text(label, text);
    }

    LvExampleEventStreak() = default;
    LvExampleEventStreak(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        lv_obj_t *info_label = lv_label_create(parent);
        lv_label_set_text(info_label, "No events yet");

        lv_obj_t *btn = lv_button_create(parent);
        lv_obj_set_size(btn, 100, 50);
        lv_obj_center(btn);
        lv_obj_add_event_cb(btn, short_click_event_cb, LV_EVENT_SHORT_CLICKED, info_label);
        lv_obj_add_event_cb(btn, streak_event_cb, LV_EVENT_SINGLE_CLICKED, "Single clicked");
        lv_obj_add_event_cb(btn, streak_event_cb, LV_EVENT_DOUBLE_CLICKED, "Double clicked");
        lv_obj_add_event_cb(btn, streak_event_cb, LV_EVENT_TRIPLE_CLICKED, "Triple clicked");

        lv_obj_t *label = lv_label_create(btn);
        lv_label_set_text(label, "Click me!");
        lv_obj_center(label);
    }
};

class LvExampleEventButton : public LvglComponensBase {
public:
    static void event_cb(lv_event_t *e)
    {
        lv_event_code_t code = lv_event_get_code(e);
        lv_obj_t *label      = lv_event_get_user_data(e);

        switch (code) {
            case LV_EVENT_PRESSED:
                lv_label_set_text(label, "The last button event:\nLV_EVENT_PRESSED");
                break;
            case LV_EVENT_CLICKED:
                lv_label_set_text(label, "The last button event:\nLV_EVENT_CLICKED");
                break;
            case LV_EVENT_LONG_PRESSED:
                lv_label_set_text(label, "The last button event:\nLV_EVENT_LONG_PRESSED");
                break;
            case LV_EVENT_LONG_PRESSED_REPEAT:
                lv_label_set_text(label, "The last button event:\nLV_EVENT_LONG_PRESSED_REPEAT");
                break;
            default:
                break;
        }
    }

    /**
     * Handle multiple events
     */
    LvExampleEventButton() = default;
    LvExampleEventButton(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        lv_obj_t *btn = lv_button_create(parent);
        lv_obj_set_size(btn, 100, 50);
        lv_obj_center(btn);

        lv_obj_t *btn_label = lv_label_create(btn);
        lv_label_set_text(btn_label, "Click me!");
        lv_obj_center(btn_label);

        lv_obj_t *info_label = lv_label_create(parent);
        lv_label_set_text(info_label, "The last button event:\nNone");

        lv_obj_add_event_cb(btn, event_cb, LV_EVENT_ALL, info_label);
    }
};

class LvExampleEventBubble : public LvglComponensBase {
public:
    static void event_cb(lv_event_t *e)
    {
        /*The original target of the event. Can be the buttons or the container*/
        lv_obj_t *target = lv_event_get_target(e);

        /*The current target is always the container as the event is added to it*/
        lv_obj_t *cont = lv_event_get_current_target(e);

        /*If container was clicked do nothing*/
        if (target == cont) return;

        /*Make the clicked buttons red*/
        lv_obj_set_style_bg_color(target, lv_palette_main(LV_PALETTE_RED), 0);
    }

    /**
     * Demonstrate event bubbling
     */
    LvExampleEventBubble() = default;
    LvExampleEventBubble(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        lv_obj_t *cont = lv_obj_create(parent);
        lv_obj_set_size(cont, 290, 200);
        lv_obj_center(cont);
        lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW_WRAP);

        uint32_t i;
        for (i = 0; i < 30; i++) {
            lv_obj_t *btn = lv_button_create(cont);
            lv_obj_set_size(btn, 70, 50);
            lv_obj_add_flag(btn, LV_OBJ_FLAG_EVENT_BUBBLE);

            lv_obj_t *label = lv_label_create(btn);
            lv_label_set_text_fmt(label, "%" LV_PRIu32, i);
            lv_obj_center(label);
        }

        lv_obj_add_event_cb(cont, event_cb, LV_EVENT_CLICKED, NULL);
    }
};

class LvExampleEventDraw : public LvglComponensBase {
public:
    inline static uint32_t size = 0;
    inline static bool size_dec = false;

    static void timer_cb(lv_timer_t *timer)
    {
        lv_obj_invalidate(lv_timer_get_user_data(timer));
        if (size_dec)
            size--;
        else
            size++;

        if (size == 50)
            size_dec = true;
        else if (size == 0)
            size_dec = false;
    }

    static void event_cb(lv_event_t *e)
    {
        lv_obj_t *obj                = lv_event_get_target(e);
        lv_draw_task_t *draw_task    = lv_event_get_draw_task(e);
        lv_draw_dsc_base_t *base_dsc = lv_draw_task_get_draw_dsc(draw_task);
        if (base_dsc->part == LV_PART_MAIN) {
            lv_draw_rect_dsc_t draw_dsc;
            lv_draw_rect_dsc_init(&draw_dsc);
            draw_dsc.bg_color      = lv_color_hex(0xffaaaa);
            draw_dsc.radius        = LV_RADIUS_CIRCLE;
            draw_dsc.border_color  = lv_color_hex(0xff5555);
            draw_dsc.border_width  = 2;
            draw_dsc.outline_color = lv_color_hex(0xff0000);
            draw_dsc.outline_pad   = 3;
            draw_dsc.outline_width = 2;

            lv_area_t a;
            a.x1 = 0;
            a.y1 = 0;
            a.x2 = size;
            a.y2 = size;
            lv_area_t obj_coords;
            lv_obj_get_coords(obj, &obj_coords);
            lv_area_align(&obj_coords, &a, LV_ALIGN_CENTER, 0, 0);

            lv_draw_rect(base_dsc->layer, &draw_dsc, &a);
        }
    }

    /**
     * Demonstrate the usage of draw event
     */
    LvExampleEventDraw() = default;
    LvExampleEventDraw(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        lv_obj_t *cont = lv_obj_create(parent);
        lv_obj_set_size(cont, 200, 200);
        lv_obj_center(cont);
        lv_obj_add_event_cb(cont, event_cb, LV_EVENT_DRAW_TASK_ADDED, NULL);
        lv_obj_add_flag(cont, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);
        lv_timer_create(timer_cb, 30, cont);
    }
};

class LvExampleFlex1 : public LvglComponensBase {
public:
    /**
     * A simple row and a column layout with flexbox
     */
    LvExampleFlex1() = default;
    LvExampleFlex1(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        /*Create a container with ROW flex direction*/
        lv_obj_t *cont_row = lv_obj_create(parent);
        lv_obj_set_size(cont_row, 300, 75);
        lv_obj_align(cont_row, LV_ALIGN_TOP_MID, 0, 5);
        lv_obj_set_flex_flow(cont_row, LV_FLEX_FLOW_ROW);

        /*Create a container with COLUMN flex direction*/
        lv_obj_t *cont_col = lv_obj_create(parent);
        lv_obj_set_size(cont_col, 200, 150);
        lv_obj_align_to(cont_col, cont_row, LV_ALIGN_OUT_BOTTOM_MID, 0, 5);
        lv_obj_set_flex_flow(cont_col, LV_FLEX_FLOW_COLUMN);

        uint32_t i;
        for (i = 0; i < 10; i++) {
            lv_obj_t *obj;
            lv_obj_t *label;

            /*Add items to the row*/
            obj = lv_button_create(cont_row);
            lv_obj_set_size(obj, 100, LV_PCT(100));

            label = lv_label_create(obj);
            lv_label_set_text_fmt(label, "Item: %" LV_PRIu32 "", i);
            lv_obj_center(label);

            /*Add items to the column*/
            obj = lv_button_create(cont_col);
            lv_obj_set_size(obj, LV_PCT(100), LV_SIZE_CONTENT);

            label = lv_label_create(obj);
            lv_label_set_text_fmt(label, "Item: %" LV_PRIu32, i);
            lv_obj_center(label);
        }
    }
};

class LvExampleFlex2 : public LvglComponensBase {
public:
    /**
     * Arrange items in rows with wrap and place the items to get even space around them.
     */
    LvExampleFlex2() = default;
    LvExampleFlex2(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        static lv_style_t style;
        lv_style_init(&style);
        lv_style_set_flex_flow(&style, LV_FLEX_FLOW_ROW_WRAP);
        lv_style_set_flex_main_place(&style, LV_FLEX_ALIGN_SPACE_EVENLY);
        lv_style_set_layout(&style, LV_LAYOUT_FLEX);

        lv_obj_t *cont = lv_obj_create(parent);
        lv_obj_set_size(cont, 300, 220);
        lv_obj_center(cont);
        lv_obj_add_style(cont, &style, 0);

        uint32_t i;
        for (i = 0; i < 8; i++) {
            lv_obj_t *obj = lv_obj_create(cont);
            lv_obj_set_size(obj, 70, LV_SIZE_CONTENT);
            lv_obj_add_flag(obj, LV_OBJ_FLAG_CHECKABLE);

            lv_obj_t *label = lv_label_create(obj);
            lv_label_set_text_fmt(label, "%" LV_PRIu32, i);
            lv_obj_center(label);
        }
    }
};

class LvExampleFlex3 : public LvglComponensBase {
public:
    /**
     * Demonstrate flex grow.
     */
    LvExampleFlex3() = default;
    LvExampleFlex3(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        lv_obj_t *cont = lv_obj_create(parent);
        lv_obj_set_size(cont, 300, 220);
        lv_obj_center(cont);
        lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW);

        lv_obj_t *obj;
        obj = lv_obj_create(cont);
        lv_obj_set_size(obj, 40, 40); /*Fix size*/

        obj = lv_obj_create(cont);
        lv_obj_set_height(obj, 40);
        lv_obj_set_flex_grow(obj, 1); /*1 portion from the free space*/

        obj = lv_obj_create(cont);
        lv_obj_set_height(obj, 40);
        lv_obj_set_flex_grow(obj, 2); /*2 portion from the free space*/

        obj = lv_obj_create(cont);
        lv_obj_set_size(obj, 40, 40); /*Fix size. It is flushed to the right by the "grow" items*/
    }
};

class LvExampleFlex4 : public LvglComponensBase {
public:
    /**
     * Reverse the order of flex items
     */
    LvExampleFlex4() = default;
    LvExampleFlex4(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        lv_obj_t *cont = lv_obj_create(parent);
        lv_obj_set_size(cont, 300, 220);
        lv_obj_center(cont);
        lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN_REVERSE);

        uint32_t i;
        for (i = 0; i < 6; i++) {
            lv_obj_t *obj = lv_obj_create(cont);
            lv_obj_set_size(obj, 100, 50);

            lv_obj_t *label = lv_label_create(obj);
            lv_label_set_text_fmt(label, "Item: %" LV_PRIu32, i);
            lv_obj_center(label);
        }
    }
};

class LvExampleFlex5 : public LvglComponensBase {
public:
    static void row_gap_anim(void *obj, int32_t v)
    {
        lv_obj_set_style_pad_row(obj, v, 0);
    }

    static void column_gap_anim(void *obj, int32_t v)
    {
        lv_obj_set_style_pad_column(obj, v, 0);
    }

    /**
     * Demonstrate the effect of column and row gap style properties
     */
    LvExampleFlex5() = default;
    LvExampleFlex5(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        lv_obj_t *cont = lv_obj_create(parent);
        lv_obj_set_size(cont, 300, 220);
        lv_obj_center(cont);
        lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW_WRAP);

        uint32_t i;
        for (i = 0; i < 9; i++) {
            lv_obj_t *obj = lv_obj_create(cont);
            lv_obj_set_size(obj, 70, LV_SIZE_CONTENT);

            lv_obj_t *label = lv_label_create(obj);
            lv_label_set_text_fmt(label, "%" LV_PRIu32, i);
            lv_obj_center(label);
        }

        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, cont);
        lv_anim_set_values(&a, 0, 10);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);

        lv_anim_set_exec_cb(&a, row_gap_anim);
        lv_anim_set_duration(&a, 500);
        lv_anim_set_playback_duration(&a, 500);
        lv_anim_start(&a);

        lv_anim_set_exec_cb(&a, column_gap_anim);
        lv_anim_set_duration(&a, 3000);
        lv_anim_set_playback_duration(&a, 3000);
        lv_anim_start(&a);
    }
};

class LvExampleFlex6 : public LvglComponensBase {
public:
    /**
     * RTL base direction changes order of the items.
     * Also demonstrate how horizontal scrolling works with RTL.
     */
    LvExampleFlex6() = default;
    LvExampleFlex6(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        lv_obj_t *cont = lv_obj_create(parent);
        lv_obj_set_style_base_dir(cont, LV_BASE_DIR_RTL, 0);
        lv_obj_set_size(cont, 300, 220);
        lv_obj_center(cont);
        lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW_WRAP);

        uint32_t i;
        for (i = 0; i < 20; i++) {
            lv_obj_t *obj = lv_obj_create(cont);
            lv_obj_set_size(obj, 70, LV_SIZE_CONTENT);

            lv_obj_t *label = lv_label_create(obj);
            lv_label_set_text_fmt(label, "%" LV_PRIu32, i);
            lv_obj_center(label);
        }
    }
};

class LvExampleGrid1 : public LvglComponensBase {
public:
    /**
     * A simple grid
     */
    LvExampleGrid1() = default;
    LvExampleGrid1(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        static int32_t col_dsc[] = {70, 70, 70, LV_GRID_TEMPLATE_LAST};
        static int32_t row_dsc[] = {50, 50, 50, LV_GRID_TEMPLATE_LAST};

        /*Create a container with grid*/
        lv_obj_t *cont = lv_obj_create(parent);
        lv_obj_set_style_grid_column_dsc_array(cont, col_dsc, 0);
        lv_obj_set_style_grid_row_dsc_array(cont, row_dsc, 0);
        lv_obj_set_size(cont, 300, 220);
        lv_obj_center(cont);
        lv_obj_set_layout(cont, LV_LAYOUT_GRID);

        lv_obj_t *label;
        lv_obj_t *obj;

        uint32_t i;
        for (i = 0; i < 9; i++) {
            uint8_t col = i % 3;
            uint8_t row = i / 3;

            obj = lv_button_create(cont);
            /*Stretch the cell horizontally and vertically too
             *Set span to 1 to make the cell 1 column/row sized*/
            lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_STRETCH, col, 1, LV_GRID_ALIGN_STRETCH, row, 1);

            label = lv_label_create(obj);
            lv_label_set_text_fmt(label, "c%d, r%d", col, row);
            lv_obj_center(label);
        }
    }
};

class LvExampleGrid2 : public LvglComponensBase {
public:
    /**
     * Demonstrate cell placement and span
     */
    LvExampleGrid2() = default;
    LvExampleGrid2(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        static int32_t col_dsc[] = {70, 70, 70, LV_GRID_TEMPLATE_LAST};
        static int32_t row_dsc[] = {50, 50, 50, LV_GRID_TEMPLATE_LAST};

        /*Create a container with grid*/
        lv_obj_t *cont = lv_obj_create(parent);
        lv_obj_set_grid_dsc_array(cont, col_dsc, row_dsc);
        lv_obj_set_size(cont, 300, 220);
        lv_obj_center(cont);

        lv_obj_t *label;
        lv_obj_t *obj;

        /*Cell to 0;0 and align to to the start (left/top) horizontally and vertically too*/
        obj = lv_obj_create(cont);
        lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_START, 0, 1);
        label = lv_label_create(obj);
        lv_label_set_text(label, "c0, r0");

        /*Cell to 1;0 and align to to the start (left) horizontally and center vertically too*/
        obj = lv_obj_create(cont);
        lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 1, 1, LV_GRID_ALIGN_CENTER, 0, 1);
        label = lv_label_create(obj);
        lv_label_set_text(label, "c1, r0");

        /*Cell to 2;0 and align to to the start (left) horizontally and end (bottom) vertically too*/
        obj = lv_obj_create(cont);
        lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 2, 1, LV_GRID_ALIGN_END, 0, 1);
        label = lv_label_create(obj);
        lv_label_set_text(label, "c2, r0");

        /*Cell to 1;1 but 2 column wide (span = 2).Set width and height to stretched.*/
        obj = lv_obj_create(cont);
        lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_STRETCH, 1, 2, LV_GRID_ALIGN_STRETCH, 1, 1);
        label = lv_label_create(obj);
        lv_label_set_text(label, "c1-2, r1");

        /*Cell to 0;1 but 2 rows tall (span = 2).Set width and height to stretched.*/
        obj = lv_obj_create(cont);
        lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 1, 2);
        label = lv_label_create(obj);
        lv_label_set_text(label, "c0\nr1-2");
    }
};

class LvExampleGrid3 : public LvglComponensBase {
public:
    /**
     * Demonstrate grid's "free unit"
     */
    LvExampleGrid3() = default;
    LvExampleGrid3(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        /*Column 1: fix width 60 px
         *Column 2: 1 unit from the remaining free space
         *Column 3: 2 unit from the remaining free space*/
        static int32_t col_dsc[] = {60, LV_GRID_FR(1), LV_GRID_FR(2), LV_GRID_TEMPLATE_LAST};

        /*Row 1: fix width 50 px
         *Row 2: 1 unit from the remaining free space
         *Row 3: fix width 50 px*/
        static int32_t row_dsc[] = {50, LV_GRID_FR(1), 50, LV_GRID_TEMPLATE_LAST};

        /*Create a container with grid*/
        lv_obj_t *cont = lv_obj_create(parent);
        lv_obj_set_size(cont, 300, 220);
        lv_obj_center(cont);
        lv_obj_set_grid_dsc_array(cont, col_dsc, row_dsc);

        lv_obj_t *label;
        lv_obj_t *obj;
        uint32_t i;
        for (i = 0; i < 9; i++) {
            uint8_t col = i % 3;
            uint8_t row = i / 3;

            obj = lv_obj_create(cont);
            /*Stretch the cell horizontally and vertically too
             *Set span to 1 to make the cell 1 column/row sized*/
            lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_STRETCH, col, 1, LV_GRID_ALIGN_STRETCH, row, 1);

            label = lv_label_create(obj);
            lv_label_set_text_fmt(label, "%d,%d", col, row);
            lv_obj_center(label);
        }
    }
};

class LvExampleGrid4 : public LvglComponensBase {
public:
    /**
     * Demonstrate track placement
     */
    LvExampleGrid4() = default;
    LvExampleGrid4(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        static int32_t col_dsc[] = {60, 60, 60, LV_GRID_TEMPLATE_LAST};
        static int32_t row_dsc[] = {45, 45, 45, LV_GRID_TEMPLATE_LAST};

        /*Add space between the columns and move the rows to the bottom (end)*/

        /*Create a container with grid*/
        lv_obj_t *cont = lv_obj_create(parent);
        lv_obj_set_grid_align(cont, LV_GRID_ALIGN_SPACE_BETWEEN, LV_GRID_ALIGN_END);
        lv_obj_set_grid_dsc_array(cont, col_dsc, row_dsc);
        lv_obj_set_size(cont, 300, 220);
        lv_obj_center(cont);

        lv_obj_t *label;
        lv_obj_t *obj;
        uint32_t i;
        for (i = 0; i < 9; i++) {
            uint8_t col = i % 3;
            uint8_t row = i / 3;

            obj = lv_obj_create(cont);
            /*Stretch the cell horizontally and vertically too
             *Set span to 1 to make the cell 1 column/row sized*/
            lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_STRETCH, col, 1, LV_GRID_ALIGN_STRETCH, row, 1);

            label = lv_label_create(obj);
            lv_label_set_text_fmt(label, "%d,%d", col, row);
            lv_obj_center(label);
        }
    }
};

class LvExampleGrid5 : public LvglComponensBase {
public:
    static void row_gap_anim(void *obj, int32_t v)
    {
        lv_obj_set_style_pad_row(obj, v, 0);
    }

    static void column_gap_anim(void *obj, int32_t v)
    {
        lv_obj_set_style_pad_column(obj, v, 0);
    }

    /**
     * Demonstrate column and row gap
     */
    LvExampleGrid5() = default;
    LvExampleGrid5(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        /*60x60 cells*/
        static int32_t col_dsc[] = {60, 60, 60, LV_GRID_TEMPLATE_LAST};
        static int32_t row_dsc[] = {45, 45, 45, LV_GRID_TEMPLATE_LAST};

        /*Create a container with grid*/
        lv_obj_t *cont = lv_obj_create(parent);
        lv_obj_set_size(cont, 300, 220);
        lv_obj_center(cont);
        lv_obj_set_grid_dsc_array(cont, col_dsc, row_dsc);

        lv_obj_t *label;
        lv_obj_t *obj;
        uint32_t i;
        for (i = 0; i < 9; i++) {
            uint8_t col = i % 3;
            uint8_t row = i / 3;

            obj = lv_obj_create(cont);
            lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_STRETCH, col, 1, LV_GRID_ALIGN_STRETCH, row, 1);
            label = lv_label_create(obj);
            lv_label_set_text_fmt(label, "%d,%d", col, row);
            lv_obj_center(label);
        }

        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, cont);
        lv_anim_set_values(&a, 0, 10);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);

        lv_anim_set_exec_cb(&a, row_gap_anim);
        lv_anim_set_duration(&a, 500);
        lv_anim_set_playback_duration(&a, 500);
        lv_anim_start(&a);

        lv_anim_set_exec_cb(&a, column_gap_anim);
        lv_anim_set_duration(&a, 3000);
        lv_anim_set_playback_duration(&a, 3000);
        lv_anim_start(&a);
    }
};

class LvExampleGrid6 : public LvglComponensBase {
public:
    /**
     * Demonstrate RTL direction on grid
     */
    LvExampleGrid6() = default;
    LvExampleGrid6(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        static int32_t col_dsc[] = {60, 60, 60, LV_GRID_TEMPLATE_LAST};
        static int32_t row_dsc[] = {45, 45, 45, LV_GRID_TEMPLATE_LAST};

        /*Create a container with grid*/
        lv_obj_t *cont = lv_obj_create(parent);
        lv_obj_set_size(cont, 300, 220);
        lv_obj_center(cont);
        lv_obj_set_style_base_dir(cont, LV_BASE_DIR_RTL, 0);
        lv_obj_set_grid_dsc_array(cont, col_dsc, row_dsc);

        lv_obj_t *label;
        lv_obj_t *obj;
        uint32_t i;
        for (i = 0; i < 9; i++) {
            uint8_t col = i % 3;
            uint8_t row = i / 3;

            obj = lv_obj_create(cont);
            /*Stretch the cell horizontally and vertically too
             *Set span to 1 to make the cell 1 column/row sized*/
            lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_STRETCH, col, 1, LV_GRID_ALIGN_STRETCH, row, 1);

            label = lv_label_create(obj);
            lv_label_set_text_fmt(label, "%d,%d", col, row);
            lv_obj_center(label);
        }
    }
};

class LvExampleScroll1 : public LvglComponensBase {
public:
    inline static lv_obj_t *panel;
    inline static lv_obj_t *save_button;
    inline static lv_obj_t *restore_button;
    inline static int saved_scroll_x;
    inline static int saved_scroll_y;

    static void scroll_update_cb(lv_event_t *e);
    static void button_event_cb(lv_event_t *e);

    static void scroll_update_cb(lv_event_t *e)
    {
        LV_UNUSED(e);

        LV_LOG("scroll info: x:%3" LV_PRId32 ", y:%3" LV_PRId32 ", top:%3" LV_PRId32
               ", "
               "bottom:%3" LV_PRId32 ", left:%3" LV_PRId32 ", right:%3" LV_PRId32 "\n",
               lv_obj_get_scroll_x(panel), lv_obj_get_scroll_y(panel), lv_obj_get_scroll_top(panel),
               lv_obj_get_scroll_bottom(panel), lv_obj_get_scroll_left(panel), lv_obj_get_scroll_right(panel));
    }

    static void button_event_cb(lv_event_t *e)
    {
        lv_obj_t *obj = lv_event_get_target_obj(e);

        if (obj == save_button) {
            saved_scroll_x = lv_obj_get_scroll_x(panel);
            saved_scroll_y = lv_obj_get_scroll_y(panel);
        } else {
            lv_obj_scroll_to(panel, saved_scroll_x, saved_scroll_y, LV_ANIM_ON);
        }
    }

    /**
     * Demonstrate how scrolling appears automatically
     */
    LvExampleScroll1() = default;
    LvExampleScroll1(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        /*Create an object with the new style*/
        lv_obj_t *scr;
        scr   = parent;
        panel = lv_obj_create(scr);
        lv_obj_set_size(panel, 200, 200);
        lv_obj_align(panel, LV_ALIGN_CENTER, 44, 0);

        lv_obj_t *child;
        lv_obj_t *label;

        child = lv_obj_create(panel);
        lv_obj_set_pos(child, 0, 0);
        lv_obj_set_size(child, 70, 70);
        label = lv_label_create(child);
        lv_label_set_text(label, "Zero");
        lv_obj_center(label);

        child = lv_obj_create(panel);
        lv_obj_set_pos(child, 160, 80);
        lv_obj_set_size(child, 80, 80);

        lv_obj_t *child2 = lv_button_create(child);
        lv_obj_set_size(child2, 100, 50);

        label = lv_label_create(child2);
        lv_label_set_text(label, "Right");
        lv_obj_center(label);

        child = lv_obj_create(panel);
        lv_obj_set_pos(child, 40, 160);
        lv_obj_set_size(child, 100, 70);
        label = lv_label_create(child);
        lv_label_set_text(label, "Bottom");
        lv_obj_center(label);

        /* When LV_OBJ_FLAG_SCROLL_ELASTIC is cleared, scrolling does not go past edge bounaries. */
        /* lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLL_ELASTIC); */

        /* Call `scroll_update_cb` while panel is being scrolled. */
        lv_obj_add_event_cb(panel, scroll_update_cb, LV_EVENT_SCROLL, NULL);

        /* Set up buttons that save and restore scroll position. */
        save_button    = lv_button_create(scr);
        restore_button = lv_button_create(scr);
        lv_obj_t *lbl;
        lbl = lv_label_create(save_button);
        lv_label_set_text_static(lbl, "Save");
        lbl = lv_label_create(restore_button);
        lv_label_set_text_static(lbl, "Restore");
        lv_obj_align_to(save_button, panel, LV_ALIGN_OUT_LEFT_MID, -10, -20);
        lv_obj_align_to(restore_button, panel, LV_ALIGN_OUT_LEFT_MID, -10, 20);
        lv_obj_add_event_cb(save_button, button_event_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_add_event_cb(restore_button, button_event_cb, LV_EVENT_CLICKED, NULL);
    }
};

class LvExampleScroll2 : public LvglComponensBase {
public:
    static void sw_event_cb(lv_event_t *e)
    {
        lv_event_code_t code = lv_event_get_code(e);
        lv_obj_t *sw         = lv_event_get_target(e);

        if (code == LV_EVENT_VALUE_CHANGED) {
            lv_obj_t *list = lv_event_get_user_data(e);

            if (lv_obj_has_state(sw, LV_STATE_CHECKED))
                lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLL_ONE);
            else
                lv_obj_remove_flag(list, LV_OBJ_FLAG_SCROLL_ONE);
        }
    }

    /**
     * Show an example to scroll snap
     */
    LvExampleScroll2() = default;
    LvExampleScroll2(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        lv_obj_t *panel = lv_obj_create(parent);
        lv_obj_set_size(panel, 280, 120);
        lv_obj_set_scroll_snap_x(panel, LV_SCROLL_SNAP_CENTER);
        lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_ROW);
        lv_obj_align(panel, LV_ALIGN_CENTER, 0, 20);

        uint32_t i;
        for (i = 0; i < 10; i++) {
            lv_obj_t *btn = lv_button_create(panel);
            lv_obj_set_size(btn, 150, lv_pct(100));

            lv_obj_t *label = lv_label_create(btn);
            if (i == 3) {
                lv_label_set_text_fmt(label, "Panel %" LV_PRIu32 "\nno snap", i);
                lv_obj_remove_flag(btn, LV_OBJ_FLAG_SNAPPABLE);
            } else {
                lv_label_set_text_fmt(label, "Panel %" LV_PRIu32, i);
            }

            lv_obj_center(label);
        }
        lv_obj_update_snap(panel, LV_ANIM_ON);

#if LV_USE_SWITCH
        /*Switch between "One scroll" and "Normal scroll" mode*/
        lv_obj_t *sw = lv_switch_create(parent);
        lv_obj_align(sw, LV_ALIGN_TOP_RIGHT, -20, 10);
        lv_obj_add_event_cb(sw, sw_event_cb, LV_EVENT_ALL, panel);
        lv_obj_t *label = lv_label_create(parent);
        lv_label_set_text(label, "One scroll");
        lv_obj_align_to(label, sw, LV_ALIGN_OUT_BOTTOM_MID, 0, 5);
#endif
    }
};

class LvExampleScroll3 : public LvglComponensBase {
public:
    inline static uint32_t btn_cnt = 1;

    static void float_button_event_cb(lv_event_t *e)
    {
        lv_event_code_t code = lv_event_get_code(e);
        lv_obj_t *float_btn  = lv_event_get_target(e);

        if (code == LV_EVENT_CLICKED) {
            lv_obj_t *list = lv_event_get_user_data(e);
            char buf[32];
            lv_snprintf(buf, sizeof(buf), "Track %d", (int)btn_cnt);
            lv_obj_t *list_btn = lv_list_add_button(list, LV_SYMBOL_AUDIO, buf);
            btn_cnt++;

            lv_obj_move_foreground(float_btn);

            lv_obj_scroll_to_view(list_btn, LV_ANIM_ON);
        }
    }

    /**
     * Create a list with a floating button
     */
    LvExampleScroll3() = default;
    LvExampleScroll3(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        lv_obj_t *list = lv_list_create(parent);
        lv_obj_set_size(list, 280, 220);
        lv_obj_center(list);

        for (btn_cnt = 1; btn_cnt <= 2; btn_cnt++) {
            char buf[32];
            lv_snprintf(buf, sizeof(buf), "Track %d", (int)btn_cnt);
            lv_list_add_button(list, LV_SYMBOL_AUDIO, buf);
        }

        lv_obj_t *float_btn = lv_button_create(list);
        lv_obj_set_size(float_btn, 50, 50);
        lv_obj_add_flag(float_btn, LV_OBJ_FLAG_FLOATING);
        lv_obj_align(float_btn, LV_ALIGN_BOTTOM_RIGHT, 0, -lv_obj_get_style_pad_right(list, LV_PART_MAIN));
        lv_obj_add_event_cb(float_btn, float_button_event_cb, LV_EVENT_ALL, list);
        lv_obj_set_style_radius(float_btn, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_image_src(float_btn, LV_SYMBOL_PLUS, 0);
        lv_obj_set_style_text_font(float_btn, lv_theme_get_font_large(float_btn), 0);
    }
};

class LvExampleScroll4 : public LvglComponensBase {
public:
    /**
     * Styling the scrollbars
     */
    LvExampleScroll4() = default;
    LvExampleScroll4(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        lv_obj_t *obj = lv_obj_create(parent);
        lv_obj_set_size(obj, 200, 100);
        lv_obj_center(obj);

        lv_obj_t *label = lv_label_create(obj);
        lv_label_set_text(
            label,
            "Lorem ipsum dolor sit amet, consectetur adipiscing elit.\n"
            "Etiam dictum, tortor vestibulum lacinia laoreet, mi neque consectetur neque, vel mattis odio dolor "
            "egestas ligula. \n"
            "Sed vestibulum sapien nulla, id convallis ex porttitor nec. \n"
            "Duis et massa eu libero accumsan faucibus a in arcu. \n"
            "Ut pulvinar odio lorem, vel tempus turpis condimentum quis. Nam consectetur condimentum sem in auctor. \n"
            "Sed nisl augue, venenatis in blandit et, gravida ac tortor. \n"
            "Etiam dapibus elementum suscipit. \n"
            "Proin mollis sollicitudin convallis. \n"
            "Integer dapibus tempus arcu nec viverra. \n"
            "Donec molestie nulla enim, eu interdum velit placerat quis. \n"
            "Donec id efficitur risus, at molestie turpis. \n"
            "Suspendisse vestibulum consectetur nunc ut commodo. \n"
            "Fusce molestie rhoncus nisi sit amet tincidunt. \n"
            "Suspendisse a nunc ut magna ornare volutpat.");

        /*Remove the style of scrollbar to have clean start*/
        lv_obj_remove_style(obj, NULL, LV_PART_SCROLLBAR | LV_STATE_ANY);

        /*Create a transition the animate the some properties on state change*/
        static const lv_style_prop_t props[] = {LV_STYLE_BG_OPA, LV_STYLE_WIDTH, 0};
        static lv_style_transition_dsc_t trans;
        lv_style_transition_dsc_init(&trans, props, lv_anim_path_linear, 200, 0, NULL);

        /*Create a style for the scrollbars*/
        static lv_style_t style;
        lv_style_init(&style);
        lv_style_set_width(&style, 4);     /*Width of the scrollbar*/
        lv_style_set_pad_right(&style, 5); /*Space from the parallel side*/
        lv_style_set_pad_top(&style, 5);   /*Space from the perpendicular side*/

        lv_style_set_radius(&style, 2);
        lv_style_set_bg_opa(&style, LV_OPA_70);
        lv_style_set_bg_color(&style, lv_palette_main(LV_PALETTE_BLUE));
        lv_style_set_border_color(&style, lv_palette_darken(LV_PALETTE_BLUE, 3));
        lv_style_set_border_width(&style, 2);
        lv_style_set_shadow_width(&style, 8);
        lv_style_set_shadow_spread(&style, 2);
        lv_style_set_shadow_color(&style, lv_palette_darken(LV_PALETTE_BLUE, 1));

        lv_style_set_transition(&style, &trans);

        /*Make the scrollbars wider and use 100% opacity when scrolled*/
        static lv_style_t style_scrolled;
        lv_style_init(&style_scrolled);
        lv_style_set_width(&style_scrolled, 8);
        lv_style_set_bg_opa(&style_scrolled, LV_OPA_COVER);

        lv_obj_add_style(obj, &style, LV_PART_SCROLLBAR);
        lv_obj_add_style(obj, &style_scrolled, LV_PART_SCROLLBAR | LV_STATE_SCROLLED);
    }
};

class LvExampleScroll5 : public LvglComponensBase {
public:
    /**
     * Scrolling with Right To Left base direction
     */
    LvExampleScroll5() = default;
    LvExampleScroll5(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        lv_obj_t *obj = lv_obj_create(parent);
        lv_obj_set_style_base_dir(obj, LV_BASE_DIR_RTL, 0);
        lv_obj_set_size(obj, 200, 100);
        lv_obj_center(obj);

        lv_obj_t *label = lv_label_create(obj);
        lv_label_set_text(label,
                          "میکروکُنترولر (به انگلیسی: Microcontroller) گونه‌ای ریزپردازنده است که دارای حافظهٔ "
                          "دسترسی "
                          "تصادفی (RAM) و "
                          "حافظهٔ فقط‌خواندنی (ROM)، تایمر، پورت‌های ورودی و خروجی (I/O) و درگاه ترتیبی "
                          "(Serial "
                          "Port پورت سریال)، "
                          "درون خود تراشه است، و می‌تواند به تنهایی ابزارهای دیگر را کنترل کند. به عبارت دیگر "
                          "یک "
                          "میکروکنترلر، مدار "
                          "مجتمع کوچکی است که از یک CPU کوچک و اجزای دیگری مانند تایمر، درگاه‌های ورودی و "
                          "خروجی "
                          "آنالوگ و دیجیتال و "
                          "حافظه تشکیل شده‌است.");
        lv_obj_set_width(label, 400);
        lv_obj_set_style_text_font(label, &lv_font_dejavu_16_persian_hebrew, 0);
    }
};

class LvExampleScroll6 : public LvglComponensBase {
public:
    static void scroll_event_cb(lv_event_t *e)
    {
        lv_obj_t *cont = lv_event_get_target(e);

        lv_area_t cont_a;
        lv_obj_get_coords(cont, &cont_a);
        int32_t cont_y_center = cont_a.y1 + lv_area_get_height(&cont_a) / 2;

        int32_t r = lv_obj_get_height(cont) * 7 / 10;
        uint32_t i;
        uint32_t child_cnt = lv_obj_get_child_count(cont);
        for (i = 0; i < child_cnt; i++) {
            lv_obj_t *child = lv_obj_get_child(cont, i);
            lv_area_t child_a;
            lv_obj_get_coords(child, &child_a);

            int32_t child_y_center = child_a.y1 + lv_area_get_height(&child_a) / 2;

            int32_t diff_y = child_y_center - cont_y_center;
            diff_y         = LV_ABS(diff_y);

            /*Get the x of diff_y on a circle.*/
            int32_t x;
            /*If diff_y is out of the circle use the last point of the circle (the radius)*/
            if (diff_y >= r) {
                x = r;
            } else {
                /*Use Pythagoras theorem to get x from radius and y*/
                uint32_t x_sqr = r * r - diff_y * diff_y;
                lv_sqrt_res_t res;
                lv_sqrt(x_sqr, &res, 0x8000); /*Use lvgl's built in sqrt root function*/
                x = r - res.i;
            }

            /*Translate the item by the calculated X coordinate*/
            lv_obj_set_style_translate_x(child, x, 0);

            /*Use some opacity with larger translations*/
            lv_opa_t opa = lv_map(x, 0, r, LV_OPA_TRANSP, LV_OPA_COVER);
            lv_obj_set_style_opa(child, LV_OPA_COVER - opa, 0);
        }
    }

    /**
     * Translate the object as they scroll
     */
    LvExampleScroll6() = default;
    LvExampleScroll6(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        lv_obj_t *cont = lv_obj_create(parent);
        lv_obj_set_size(cont, 200, 200);
        lv_obj_center(cont);
        lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
        lv_obj_add_event_cb(cont, scroll_event_cb, LV_EVENT_SCROLL, NULL);
        lv_obj_set_style_radius(cont, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_clip_corner(cont, true, 0);
        lv_obj_set_scroll_dir(cont, LV_DIR_VER);
        lv_obj_set_scroll_snap_y(cont, LV_SCROLL_SNAP_CENTER);
        lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_OFF);

        uint32_t i;
        for (i = 0; i < 20; i++) {
            lv_obj_t *btn = lv_button_create(cont);
            lv_obj_set_width(btn, lv_pct(100));

            lv_obj_t *label = lv_label_create(btn);
            lv_label_set_text_fmt(label, "Button %" LV_PRIu32, i);
        }

        /*Update the buttons position manually for first*/
        lv_obj_send_event(cont, LV_EVENT_SCROLL, NULL);

        /*Be sure the fist button is in the middle*/
        lv_obj_scroll_to_view(lv_obj_get_child(cont, 0), LV_ANIM_OFF);
    }
};

class LvExampleObj1 : public LvglComponensBase {
public:
    LvExampleObj1() = default;
    LvExampleObj1(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        lv_obj_t *obj1;
        obj1 = lv_obj_create(parent);
        lv_obj_set_size(obj1, 100, 50);
        lv_obj_align(obj1, LV_ALIGN_CENTER, -60, -30);

        static lv_style_t style_shadow;
        lv_style_init(&style_shadow);
        lv_style_set_shadow_width(&style_shadow, 10);
        lv_style_set_shadow_spread(&style_shadow, 5);
        lv_style_set_shadow_color(&style_shadow, lv_palette_main(LV_PALETTE_BLUE));

        lv_obj_t *obj2;
        obj2 = lv_obj_create(parent);
        lv_obj_add_style(obj2, &style_shadow, 0);
        lv_obj_align(obj2, LV_ALIGN_CENTER, 60, 30);
    }
};

class LvExampleObj2 : public LvglComponensBase {
public:
    static void drag_event_handler(lv_event_t *e)
    {
        lv_obj_t *obj = lv_event_get_target(e);

        lv_indev_t *indev = lv_indev_active();
        if (indev == NULL) return;

        lv_point_t vect;
        lv_indev_get_vect(indev, &vect);

        int32_t x = lv_obj_get_x_aligned(obj) + vect.x;
        int32_t y = lv_obj_get_y_aligned(obj) + vect.y;
        lv_obj_set_pos(obj, x, y);
    }

    /**
     * Make an object draggable.
     */
    LvExampleObj2() = default;
    LvExampleObj2(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        lv_obj_t *obj;
        obj = lv_obj_create(parent);
        lv_obj_set_size(obj, 150, 100);
        lv_obj_add_event_cb(obj, drag_event_handler, LV_EVENT_PRESSING, NULL);

        lv_obj_t *label = lv_label_create(obj);
        lv_label_set_text(label, "Drag me");
        lv_obj_center(label);
    }
};

class LvExampleObj3 : public LvglComponensBase {
public:
#if LV_DRAW_TRANSFORM_USE_MATRIX

    static void timer_cb(lv_timer_t *timer)
    {
        lv_obj_t *obj = lv_timer_get_user_data(timer);

        static float value = 0.1f;
        lv_matrix_t matrix;
        lv_matrix_identity(&matrix);
        lv_matrix_scale(&matrix, value, 1);
        lv_matrix_rotate(&matrix, value * 360);
        lv_obj_set_transform(obj, &matrix);

        value += 0.01f;

        if (value > 2.0f) {
            lv_obj_reset_transform(obj);
            value = 0.1f;
        }
    }

    LvExampleObj3() = default;
    LvExampleObj3(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        lv_obj_t *obj = lv_obj_create(parent);
        lv_obj_center(obj);

        lv_timer_create(timer_cb, 20, obj);
    }

#else

    void lv_example_obj_3(void)
    {
        lv_obj_t *label = lv_label_create(lv_screen_active());
        lv_label_set_text_static(label, "LV_DRAW_TRANSFORM_USE_MATRIX is not enabled");
        lv_obj_center(label);
    }

#endif /*LV_DRAW_TRANSFORM_USE_MATRIX*/
};

class LvExampleArc1 : public LvglComponensBase {
public:
    static void value_changed_event_cb(lv_event_t *e);

    LvExampleArc1() = default;
    LvExampleArc1(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        lv_obj_t *label = lv_label_create(parent);

        /*Create an Arc*/
        lv_obj_t *arc = lv_arc_create(parent);
        lv_obj_set_size(arc, 150, 150);
        lv_arc_set_rotation(arc, 135);
        lv_arc_set_bg_angles(arc, 0, 270);
        lv_arc_set_value(arc, 10);
        lv_obj_center(arc);
        lv_obj_add_event_cb(arc, value_changed_event_cb, LV_EVENT_VALUE_CHANGED, label);

        /*Manually update the label for the first time*/
        lv_obj_send_event(arc, LV_EVENT_VALUE_CHANGED, NULL);
    }

    static void value_changed_event_cb(lv_event_t *e)
    {
        lv_obj_t *arc   = lv_event_get_target(e);
        lv_obj_t *label = lv_event_get_user_data(e);

        lv_label_set_text_fmt(label, "%" LV_PRId32 "%%", lv_arc_get_value(arc));

        /*Rotate the label to the current position of the arc*/
        lv_arc_rotate_obj_to_angle(arc, label, 25);
    }
};

class LvExampleArc2 : public LvglComponensBase {
public:
    static void set_angle(void *obj, int32_t v)
    {
        lv_arc_set_value(obj, v);
    }

    /**
     * Create an arc which acts as a loader.
     */
    LvExampleArc2() = default;
    LvExampleArc2(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        /*Create an Arc*/
        lv_obj_t *arc = lv_arc_create(parent);
        lv_arc_set_rotation(arc, 270);
        lv_arc_set_bg_angles(arc, 0, 360);
        lv_obj_remove_style(arc, NULL, LV_PART_KNOB);   /*Be sure the knob is not displayed*/
        lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE); /*To not allow adjusting by click*/
        lv_obj_center(arc);

        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, arc);
        lv_anim_set_exec_cb(&a, set_angle);
        lv_anim_set_duration(&a, 1000);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE); /*Just for the demo*/
        lv_anim_set_repeat_delay(&a, 500);
        lv_anim_set_values(&a, 0, 100);
        lv_anim_start(&a);
    }
};

class LvExampleBar1 : public LvglComponensBase {
public:
    LvExampleBar1() = default;
    LvExampleBar1(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        lv_obj_t *bar1 = lv_bar_create(parent);
        lv_obj_set_size(bar1, 200, 20);
        lv_obj_center(bar1);
        lv_bar_set_value(bar1, 70, LV_ANIM_OFF);
    }
};

class LvExampleBar2 : public LvglComponensBase {
public:
    /**
     * Example of styling the bar
     */
    LvExampleBar2() = default;
    LvExampleBar2(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        static lv_style_t style_bg;
        static lv_style_t style_indic;

        lv_style_init(&style_bg);
        lv_style_set_border_color(&style_bg, lv_palette_main(LV_PALETTE_BLUE));
        lv_style_set_border_width(&style_bg, 2);
        lv_style_set_pad_all(&style_bg, 6); /*To make the indicator smaller*/
        lv_style_set_radius(&style_bg, 6);
        lv_style_set_anim_duration(&style_bg, 1000);

        lv_style_init(&style_indic);
        lv_style_set_bg_opa(&style_indic, LV_OPA_COVER);
        lv_style_set_bg_color(&style_indic, lv_palette_main(LV_PALETTE_BLUE));
        lv_style_set_radius(&style_indic, 3);

        lv_obj_t *bar = lv_bar_create(parent);
        lv_obj_remove_style_all(bar); /*To have a clean start*/
        lv_obj_add_style(bar, &style_bg, 0);
        lv_obj_add_style(bar, &style_indic, LV_PART_INDICATOR);

        lv_obj_set_size(bar, 200, 20);
        lv_obj_center(bar);
        lv_bar_set_value(bar, 100, LV_ANIM_ON);
    }
};

class LvExampleBar3 : public LvglComponensBase {
public:
    static void set_temp(void *bar, int32_t temp)
    {
        lv_bar_set_value(bar, temp, LV_ANIM_ON);
    }

    /**
     * A temperature meter example
     */
    LvExampleBar3() = default;
    LvExampleBar3(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        static lv_style_t style_indic;

        lv_style_init(&style_indic);
        lv_style_set_bg_opa(&style_indic, LV_OPA_COVER);
        lv_style_set_bg_color(&style_indic, lv_palette_main(LV_PALETTE_RED));
        lv_style_set_bg_grad_color(&style_indic, lv_palette_main(LV_PALETTE_BLUE));
        lv_style_set_bg_grad_dir(&style_indic, LV_GRAD_DIR_VER);

        lv_obj_t *bar = lv_bar_create(parent);
        lv_obj_add_style(bar, &style_indic, LV_PART_INDICATOR);
        lv_obj_set_size(bar, 20, 200);
        lv_obj_center(bar);
        lv_bar_set_range(bar, -20, 40);

        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_exec_cb(&a, set_temp);
        lv_anim_set_duration(&a, 3000);
        lv_anim_set_playback_duration(&a, 3000);
        lv_anim_set_var(&a, bar);
        lv_anim_set_values(&a, -20, 40);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
        lv_anim_start(&a);
    }
};

class LvExampleBar4 : public LvglComponensBase {
public:
    /**
     * Bar with stripe pattern and ranged value
     */
    LvExampleBar4() = default;
    LvExampleBar4(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        LV_IMAGE_DECLARE(img_skew_strip);
        static lv_style_t style_indic;

        lv_style_init(&style_indic);
        lv_style_set_bg_image_src(&style_indic, &img_skew_strip);
        lv_style_set_bg_image_tiled(&style_indic, true);
        lv_style_set_bg_image_opa(&style_indic, LV_OPA_30);

        lv_obj_t *bar = lv_bar_create(parent);
        lv_obj_add_style(bar, &style_indic, LV_PART_INDICATOR);

        lv_obj_set_size(bar, 260, 20);
        lv_obj_center(bar);
        lv_bar_set_mode(bar, LV_BAR_MODE_RANGE);
        lv_bar_set_value(bar, 90, LV_ANIM_OFF);
        lv_bar_set_start_value(bar, 20, LV_ANIM_OFF);
    }
};

class LvExampleBar5 : public LvglComponensBase {
public:
    /**
     * Bar with LTR and RTL base direction
     */
    LvExampleBar5() = default;
    LvExampleBar5(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        lv_obj_t *label;

        lv_obj_t *bar_ltr = lv_bar_create(parent);
        lv_obj_set_size(bar_ltr, 200, 20);
        lv_bar_set_value(bar_ltr, 70, LV_ANIM_OFF);
        lv_obj_align(bar_ltr, LV_ALIGN_CENTER, 0, -30);

        label = lv_label_create(parent);
        lv_label_set_text(label, "Left to Right base direction");
        lv_obj_align_to(label, bar_ltr, LV_ALIGN_OUT_TOP_MID, 0, -5);

        lv_obj_t *bar_rtl = lv_bar_create(parent);
        lv_obj_set_style_base_dir(bar_rtl, LV_BASE_DIR_RTL, 0);
        lv_obj_set_size(bar_rtl, 200, 20);
        lv_bar_set_value(bar_rtl, 70, LV_ANIM_OFF);
        lv_obj_align(bar_rtl, LV_ALIGN_CENTER, 0, 30);

        label = lv_label_create(parent);
        lv_label_set_text(label, "Right to Left base direction");
        lv_obj_align_to(label, bar_rtl, LV_ALIGN_OUT_TOP_MID, 0, -5);
    }
};

class LvExampleBar6 : public LvglComponensBase {
public:
#define MAX_VALUE 100
#define MIN_VALUE 0

    static void set_value(void *bar, int32_t v)
    {
        lv_bar_set_value(bar, v, LV_ANIM_OFF);
    }

    static void event_cb(lv_event_t *e)
    {
        lv_obj_t *obj = lv_event_get_target(e);

        lv_draw_label_dsc_t label_dsc;
        lv_draw_label_dsc_init(&label_dsc);
        label_dsc.font = LV_FONT_DEFAULT;

        char buf[8];
        lv_snprintf(buf, sizeof(buf), "%d", (int)lv_bar_get_value(obj));

        lv_point_t txt_size;
        lv_text_get_size(&txt_size, buf, label_dsc.font, label_dsc.letter_space, label_dsc.line_space, LV_COORD_MAX,
                         label_dsc.flag);

        lv_area_t txt_area;
        txt_area.x1 = 0;
        txt_area.x2 = txt_size.x - 1;
        txt_area.y1 = 0;
        txt_area.y2 = txt_size.y - 1;

        lv_area_t indic_area;
        lv_obj_get_coords(obj, &indic_area);
        lv_area_set_width(&indic_area, lv_area_get_width(&indic_area) * lv_bar_get_value(obj) / MAX_VALUE);

        /*If the indicator is long enough put the text inside on the right*/
        if (lv_area_get_width(&indic_area) > txt_size.x + 20) {
            lv_area_align(&indic_area, &txt_area, LV_ALIGN_RIGHT_MID, -10, 0);
            label_dsc.color = lv_color_white();
        }
        /*If the indicator is still short put the text out of it on the right*/
        else {
            lv_area_align(&indic_area, &txt_area, LV_ALIGN_OUT_RIGHT_MID, 10, 0);
            label_dsc.color = lv_color_black();
        }
        label_dsc.text       = buf;
        label_dsc.text_local = true;
        lv_layer_t *layer    = lv_event_get_layer(e);
        lv_draw_label(layer, &label_dsc, &txt_area);
    }

    /**
     * Custom drawer on the bar to display the current value
     */
    LvExampleBar6() = default;
    LvExampleBar6(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        lv_obj_t *bar = lv_bar_create(parent);
        lv_bar_set_range(bar, MIN_VALUE, MAX_VALUE);
        lv_obj_set_size(bar, 200, 20);
        lv_obj_center(bar);
        lv_obj_add_event_cb(bar, event_cb, LV_EVENT_DRAW_MAIN_END, NULL);

        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, bar);
        lv_anim_set_values(&a, 0, 100);
        lv_anim_set_exec_cb(&a, set_value);
        lv_anim_set_duration(&a, 4000);
        lv_anim_set_playback_duration(&a, 4000);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
        lv_anim_start(&a);
    }
};

class LvExampleBar7 : public LvglComponensBase {
public:
    /**
     * Bar with opposite direction
     */
    LvExampleBar7() = default;
    LvExampleBar7(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        lv_obj_t *label;

        lv_obj_t *bar_tob = lv_bar_create(parent);
        lv_obj_set_size(bar_tob, 20, 200);
        lv_bar_set_range(bar_tob, 100, 0);
        lv_bar_set_value(bar_tob, 70, LV_ANIM_OFF);
        lv_obj_align(bar_tob, LV_ALIGN_CENTER, 0, -30);

        label = lv_label_create(parent);
        lv_label_set_text(label, "From top to bottom");
        lv_obj_align_to(label, bar_tob, LV_ALIGN_OUT_TOP_MID, 0, -5);
    }
};

class LvExampleCalendar1 : public LvglComponensBase {
public:
    static void event_handler(lv_event_t *e)
    {
        lv_event_code_t code = lv_event_get_code(e);
        lv_obj_t *obj        = lv_event_get_current_target(e);

        if (code == LV_EVENT_VALUE_CHANGED) {
            lv_calendar_date_t date;
            if (lv_calendar_get_pressed_date(obj, &date)) {
                LV_LOG_USER("Clicked date: %02d.%02d.%d", date.day, date.month, date.year);
            }
        }
    }

    LvExampleCalendar1() = default;
    LvExampleCalendar1(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        lv_obj_t *calendar = lv_calendar_create(parent);
        lv_obj_set_size(calendar, 185, 230);
        lv_obj_align(calendar, LV_ALIGN_CENTER, 0, 27);
        lv_obj_add_event_cb(calendar, event_handler, LV_EVENT_ALL, NULL);

        lv_calendar_set_today_date(calendar, 2021, 02, 23);
        lv_calendar_set_showed_date(calendar, 2021, 02);

        /*Highlight a few days*/
        static lv_calendar_date_t highlighted_days[3]; /*Only its pointer will be saved so should be static*/
        highlighted_days[0].year  = 2021;
        highlighted_days[0].month = 02;
        highlighted_days[0].day   = 6;

        highlighted_days[1].year  = 2021;
        highlighted_days[1].month = 02;
        highlighted_days[1].day   = 11;

        highlighted_days[2].year  = 2022;
        highlighted_days[2].month = 02;
        highlighted_days[2].day   = 22;

        lv_calendar_set_highlighted_dates(calendar, highlighted_days, 3);

#if LV_USE_CALENDAR_HEADER_DROPDOWN
        lv_calendar_header_dropdown_create(calendar);
#elif LV_USE_CALENDAR_HEADER_ARROW
        lv_calendar_header_arrow_create(calendar);
#endif
    }
};

class LvExampleCalendar2 : public LvglComponensBase {
public:
    LvExampleCalendar2() = default;
    LvExampleCalendar2(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        lv_obj_t *calendar = lv_calendar_create(parent);
        lv_obj_set_size(calendar, 300, 300);
        lv_obj_align(calendar, LV_ALIGN_TOP_MID, 0, 0);

        lv_calendar_set_today_date(calendar, 2024, 03, 22);
        lv_calendar_set_showed_date(calendar, 2024, 03);

#if LV_USE_CALENDAR_HEADER_DROPDOWN
        lv_calendar_header_dropdown_create(calendar);
#elif LV_USE_CALENDAR_HEADER_ARROW
        lv_calendar_header_arrow_create(calendar);
#endif

        lv_calendar_set_chinese_mode(calendar, true);
        lv_obj_set_style_text_font(calendar, &lv_font_simsun_14_cjk, LV_PART_MAIN);
    }
};

class LvExampleCanvas1 : public LvglComponensBase {
public:
#define CANVAS_WIDTH  200
#define CANVAS_HEIGHT 150

    LvExampleCanvas1() = default;
    LvExampleCanvas1(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        lv_draw_rect_dsc_t rect_dsc;
        lv_draw_rect_dsc_init(&rect_dsc);
        rect_dsc.radius                 = 10;
        rect_dsc.bg_opa                 = LV_OPA_COVER;
        rect_dsc.bg_grad.dir            = LV_GRAD_DIR_VER;
        rect_dsc.bg_grad.stops[0].color = lv_palette_main(LV_PALETTE_RED);
        rect_dsc.bg_grad.stops[0].opa   = LV_OPA_100;
        rect_dsc.bg_grad.stops[1].color = lv_palette_main(LV_PALETTE_BLUE);
        rect_dsc.bg_grad.stops[1].opa   = LV_OPA_50;
        rect_dsc.border_width           = 2;
        rect_dsc.border_opa             = LV_OPA_90;
        rect_dsc.border_color           = lv_color_white();
        rect_dsc.shadow_width           = 5;
        rect_dsc.shadow_offset_x        = 5;
        rect_dsc.shadow_offset_y        = 5;

        lv_draw_label_dsc_t label_dsc;
        lv_draw_label_dsc_init(&label_dsc);
        label_dsc.color = lv_palette_main(LV_PALETTE_ORANGE);
        label_dsc.text  = "Some text on text canvas";
        /*Create a buffer for the canvas*/
        LV_DRAW_BUF_DEFINE_STATIC(draw_buf_16bpp, CANVAS_WIDTH, CANVAS_HEIGHT, LV_COLOR_FORMAT_RGB565);
        LV_DRAW_BUF_INIT_STATIC(draw_buf_16bpp);

        lv_obj_t *canvas = lv_canvas_create(parent);
        lv_canvas_set_draw_buf(canvas, &draw_buf_16bpp);
        lv_obj_center(canvas);
        lv_canvas_fill_bg(canvas, lv_palette_lighten(LV_PALETTE_GREY, 3), LV_OPA_COVER);

        lv_layer_t layer;
        lv_canvas_init_layer(canvas, &layer);

        lv_area_t coords_rect = {30, 20, 100, 70};
        lv_draw_rect(&layer, &rect_dsc, &coords_rect);

        lv_area_t coords_text = {40, 80, 100, 120};
        lv_draw_label(&layer, &label_dsc, &coords_text);

        lv_canvas_finish_layer(canvas, &layer);

        /*Test the rotation. It requires another buffer where the original image is stored.
         *So use previous canvas as image and rotate it to the new canvas*/
        LV_DRAW_BUF_DEFINE_STATIC(draw_buf_32bpp, CANVAS_WIDTH, CANVAS_HEIGHT, LV_COLOR_FORMAT_ARGB8888);
        LV_DRAW_BUF_INIT_STATIC(draw_buf_32bpp);

        /*Create a canvas and initialize its palette*/
        canvas = lv_canvas_create(parent);
        lv_canvas_set_draw_buf(canvas, &draw_buf_32bpp);
        lv_canvas_fill_bg(canvas, lv_color_hex3(0xccc), LV_OPA_COVER);
        lv_obj_center(canvas);

        lv_canvas_fill_bg(canvas, lv_palette_lighten(LV_PALETTE_GREY, 1), LV_OPA_COVER);

        lv_canvas_init_layer(canvas, &layer);
        lv_image_dsc_t img;
        lv_draw_buf_to_image(&draw_buf_16bpp, &img);
        lv_draw_image_dsc_t img_dsc;
        lv_draw_image_dsc_init(&img_dsc);
        img_dsc.rotation = 120;
        img_dsc.src      = &img;
        img_dsc.pivot.x  = CANVAS_WIDTH / 2;
        img_dsc.pivot.y  = CANVAS_HEIGHT / 2;

        lv_area_t coords_img = {0, 0, CANVAS_WIDTH - 1, CANVAS_HEIGHT - 1};
        lv_draw_image(&layer, &img_dsc, &coords_img);

        lv_canvas_finish_layer(canvas, &layer);
    }
};

class LvExampleCanvas2 : public LvglComponensBase {
public:
#define CANVAS_WIDTH  80
#define CANVAS_HEIGHT 40

    /**
     * Create a transparent canvas with transparency
     */
    LvExampleCanvas2() = default;
    LvExampleCanvas2(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        lv_obj_set_style_bg_color(parent, lv_palette_lighten(LV_PALETTE_RED, 5), 0);

        /*Create a buffer for the canvas*/
        LV_DRAW_BUF_DEFINE_STATIC(draw_buf, CANVAS_WIDTH, CANVAS_HEIGHT, LV_COLOR_FORMAT_ARGB8888);
        LV_DRAW_BUF_INIT_STATIC(draw_buf);

        /*Create a canvas and initialize its palette*/
        lv_obj_t *canvas = lv_canvas_create(parent);
        lv_canvas_set_draw_buf(canvas, &draw_buf);
        lv_obj_center(canvas);

        /*Red background (There is no dedicated alpha channel in indexed images so LV_OPA_COVER is ignored)*/
        lv_canvas_fill_bg(canvas, lv_palette_main(LV_PALETTE_BLUE), LV_OPA_COVER);

        /*Create hole on the canvas*/
        uint32_t x;
        uint32_t y;
        for (y = 10; y < 20; y++) {
            for (x = 5; x < 75; x++) {
                lv_canvas_set_px(canvas, x, y, lv_palette_main(LV_PALETTE_BLUE), LV_OPA_50);
            }
        }

        for (y = 20; y < 30; y++) {
            for (x = 5; x < 75; x++) {
                lv_canvas_set_px(canvas, x, y, lv_palette_main(LV_PALETTE_BLUE), LV_OPA_20);
            }
        }

        for (y = 30; y < 40; y++) {
            for (x = 5; x < 75; x++) {
                lv_canvas_set_px(canvas, x, y, lv_palette_main(LV_PALETTE_BLUE), LV_OPA_0);
            }
        }
    }
};

class LvExampleCanvas3 : public LvglComponensBase {
public:
#define CANVAS_WIDTH  50
#define CANVAS_HEIGHT 50

    /**
     * Draw a rectangle to the canvas
     */
    LvExampleCanvas3() = default;
    LvExampleCanvas3(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        /*Create a buffer for the canvas*/
        LV_DRAW_BUF_DEFINE_STATIC(draw_buf, CANVAS_WIDTH, CANVAS_HEIGHT, LV_COLOR_FORMAT_ARGB8888);
        LV_DRAW_BUF_INIT_STATIC(draw_buf);

        /*Create a canvas and initialize its palette*/
        lv_obj_t *canvas = lv_canvas_create(parent);
        lv_canvas_set_draw_buf(canvas, &draw_buf);

        lv_canvas_fill_bg(canvas, lv_color_hex3(0xccc), LV_OPA_COVER);
        lv_obj_center(canvas);

        lv_layer_t layer;
        lv_canvas_init_layer(canvas, &layer);

        lv_draw_rect_dsc_t dsc;
        lv_draw_rect_dsc_init(&dsc);
        dsc.bg_color      = lv_palette_main(LV_PALETTE_RED);
        dsc.border_color  = lv_palette_main(LV_PALETTE_BLUE);
        dsc.border_width  = 3;
        dsc.outline_color = lv_palette_main(LV_PALETTE_GREEN);
        dsc.outline_width = 2;
        dsc.outline_pad   = 2;
        dsc.outline_opa   = LV_OPA_50;
        dsc.radius        = 5;
        dsc.border_width  = 3;

        lv_area_t coords = {10, 10, 40, 30};

        lv_draw_rect(&layer, &dsc, &coords);

        lv_canvas_finish_layer(canvas, &layer);
    }
};

class LvExampleCanvas4 : public LvglComponensBase {
public:
#define CANVAS_WIDTH  50
#define CANVAS_HEIGHT 50

    /**
     * Draw a text to the canvas
     */
    LvExampleCanvas4() = default;
    LvExampleCanvas4(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        /*Create a buffer for the canvas*/
        LV_DRAW_BUF_DEFINE_STATIC(draw_buf, CANVAS_WIDTH, CANVAS_HEIGHT, LV_COLOR_FORMAT_ARGB8888);
        LV_DRAW_BUF_INIT_STATIC(draw_buf);

        /*Create a canvas and initialize its palette*/
        lv_obj_t *canvas = lv_canvas_create(parent);
        lv_canvas_set_draw_buf(canvas, &draw_buf);
        lv_canvas_fill_bg(canvas, lv_color_hex3(0xccc), LV_OPA_COVER);
        lv_obj_center(canvas);

        lv_layer_t layer;
        lv_canvas_init_layer(canvas, &layer);

        lv_draw_label_dsc_t dsc;
        lv_draw_label_dsc_init(&dsc);
        dsc.color = lv_palette_main(LV_PALETTE_RED);
        dsc.font  = &lv_font_montserrat_18;
        dsc.decor = LV_TEXT_DECOR_UNDERLINE;
        dsc.text  = "Hello";

        lv_area_t coords = {10, 10, 30, 60};

        lv_draw_label(&layer, &dsc, &coords);

        lv_canvas_finish_layer(canvas, &layer);
    }
};

class LvExampleCanvas5 : public LvglComponensBase {
public:
#define CANVAS_WIDTH  50
#define CANVAS_HEIGHT 50

    /**
     * Draw an arc to the canvas
     */
    LvExampleCanvas5() = default;
    LvExampleCanvas5(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        /*Create a buffer for the canvas*/
        LV_DRAW_BUF_DEFINE_STATIC(draw_buf, CANVAS_WIDTH, CANVAS_HEIGHT, LV_COLOR_FORMAT_ARGB8888);
        LV_DRAW_BUF_INIT_STATIC(draw_buf);

        /*Create a canvas and initialize its palette*/
        lv_obj_t *canvas = lv_canvas_create(parent);
        lv_canvas_set_draw_buf(canvas, &draw_buf);
        lv_canvas_fill_bg(canvas, lv_color_hex3(0xccc), LV_OPA_COVER);
        lv_obj_center(canvas);

        lv_layer_t layer;
        lv_canvas_init_layer(canvas, &layer);

        lv_draw_arc_dsc_t dsc;
        lv_draw_arc_dsc_init(&dsc);
        dsc.color       = lv_palette_main(LV_PALETTE_RED);
        dsc.width       = 5;
        dsc.center.x    = 25;
        dsc.center.y    = 25;
        dsc.width       = 10;
        dsc.radius      = 15;
        dsc.start_angle = 0;
        dsc.end_angle   = 220;

        lv_draw_arc(&layer, &dsc);

        lv_canvas_finish_layer(canvas, &layer);
    }
};

class LvExampleCanvas6 : public LvglComponensBase {
public:
#define CANVAS_WIDTH  50
#define CANVAS_HEIGHT 50

    /**
     * Draw an image to the canvas
     */
    LvExampleCanvas6() = default;
    LvExampleCanvas6(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        /*Create a buffer for the canvas*/
        static uint8_t cbuf[LV_CANVAS_BUF_SIZE(CANVAS_WIDTH, CANVAS_HEIGHT, 32, LV_DRAW_BUF_STRIDE_ALIGN)];

        /*Create a canvas and initialize its palette*/
        lv_obj_t *canvas = lv_canvas_create(parent);
        lv_canvas_set_buffer(canvas, cbuf, CANVAS_WIDTH, CANVAS_HEIGHT, LV_COLOR_FORMAT_ARGB8888);
        lv_canvas_fill_bg(canvas, lv_color_hex3(0xccc), LV_OPA_COVER);
        lv_obj_center(canvas);

        lv_layer_t layer;
        lv_canvas_init_layer(canvas, &layer);

        LV_IMAGE_DECLARE(img_star);
        lv_draw_image_dsc_t dsc;
        lv_draw_image_dsc_init(&dsc);
        dsc.src = &img_star;

        lv_area_t coords = {10, 10, 10 + img_star.header.w - 1, 10 + img_star.header.h - 1};

        lv_draw_image(&layer, &dsc, &coords);

        lv_canvas_finish_layer(canvas, &layer);
    }
};

class LvExampleCanvas7 : public LvglComponensBase {
public:
#define CANVAS_WIDTH  50
#define CANVAS_HEIGHT 50

    /**
     * Draw a line to the canvas
     */
    LvExampleCanvas7() = default;
    LvExampleCanvas7(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        /*Create a buffer for the canvas*/
        LV_DRAW_BUF_DEFINE_STATIC(draw_buf, CANVAS_WIDTH, CANVAS_HEIGHT, LV_COLOR_FORMAT_ARGB8888);
        LV_DRAW_BUF_INIT_STATIC(draw_buf);

        /*Create a canvas and initialize its palette*/
        lv_obj_t *canvas = lv_canvas_create(parent);
        lv_canvas_set_draw_buf(canvas, &draw_buf);
        lv_canvas_fill_bg(canvas, lv_color_hex3(0xccc), LV_OPA_COVER);
        lv_obj_center(canvas);

        lv_layer_t layer;
        lv_canvas_init_layer(canvas, &layer);

        lv_draw_line_dsc_t dsc;
        lv_draw_line_dsc_init(&dsc);
        dsc.color       = lv_palette_main(LV_PALETTE_RED);
        dsc.width       = 4;
        dsc.round_end   = 1;
        dsc.round_start = 1;
        dsc.p1.x        = 15;
        dsc.p1.y        = 15;
        dsc.p2.x        = 35;
        dsc.p2.y        = 10;
        lv_draw_line(&layer, &dsc);

        lv_canvas_finish_layer(canvas, &layer);
    }
};

class LvExampleCanvas8 : public LvglComponensBase {
public:
#if LV_USE_VECTOR_GRAPHIC

#define CANVAS_WIDTH  150
#define CANVAS_HEIGHT 150

    /**
     * Draw a path to the canvas
     */
    LvExampleCanvas8() = default;
    LvExampleCanvas8(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        /*Create a buffer for the canvas*/
        LV_DRAW_BUF_DEFINE_STATIC(draw_buf, CANVAS_WIDTH, CANVAS_HEIGHT, LV_COLOR_FORMAT_ARGB8888);
        LV_DRAW_BUF_INIT_STATIC(draw_buf);

        /*Create a canvas and initialize its palette*/
        lv_obj_t *canvas = lv_canvas_create(parent);
        lv_canvas_set_draw_buf(canvas, &draw_buf);
        lv_canvas_fill_bg(canvas, lv_color_hex3(0xccc), LV_OPA_COVER);
        lv_obj_center(canvas);

        lv_layer_t layer;
        lv_canvas_init_layer(canvas, &layer);

        lv_vector_dsc_t *dsc   = lv_vector_dsc_create(&layer);
        lv_vector_path_t *path = lv_vector_path_create(LV_VECTOR_PATH_QUALITY_MEDIUM);

        lv_fpoint_t pts[] = {{10, 10}, {130, 130}, {10, 130}};
        lv_vector_path_move_to(path, &pts[0]);
        lv_vector_path_line_to(path, &pts[1]);
        lv_vector_path_line_to(path, &pts[2]);
        lv_vector_path_close(path);

        lv_vector_dsc_set_fill_color(dsc, lv_color_make(0x00, 0x80, 0xff));
        lv_vector_dsc_add_path(dsc, path);

        lv_draw_vector(dsc);
        lv_vector_path_delete(path);
        lv_vector_dsc_delete(dsc);

        lv_canvas_finish_layer(canvas, &layer);
    }
#else

    void lv_example_canvas_8(void)
    {
        /*fallback for online examples*/
        lv_obj_t *label = lv_label_create(lv_screen_active());
        lv_label_set_text(label, "Vector graphics is not enabled");
        lv_obj_center(label);
    }

#endif /*LV_USE_VECTOR_GRAPHIC*/
};

class LvExampleChart1 : public LvglComponensBase {
public:
    /**
     * A very basic line chart
     */
    LvExampleChart1() = default;
    LvExampleChart1(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        /*Create a chart*/
        lv_obj_t *chart;
        chart = lv_chart_create(parent);
        lv_obj_set_size(chart, 200, 150);
        lv_obj_center(chart);
        lv_chart_set_type(chart, LV_CHART_TYPE_LINE); /*Show lines and points too*/

        /*Add two data series*/
        lv_chart_series_t *ser1 =
            lv_chart_add_series(chart, lv_palette_main(LV_PALETTE_GREEN), LV_CHART_AXIS_PRIMARY_Y);
        lv_chart_series_t *ser2 =
            lv_chart_add_series(chart, lv_palette_main(LV_PALETTE_RED), LV_CHART_AXIS_SECONDARY_Y);
        int32_t *ser2_y_points = lv_chart_get_y_array(chart, ser2);

        uint32_t i;
        for (i = 0; i < 10; i++) {
            /*Set the next points on 'ser1'*/
            lv_chart_set_next_value(chart, ser1, lv_rand(10, 50));

            /*Directly set points on 'ser2'*/
            ser2_y_points[i] = lv_rand(50, 90);
        }

        lv_chart_refresh(chart); /*Required after direct set*/
    }
};

class LvExampleChart2 : public LvglComponensBase {
public:
    /**
     * Use lv_scale to add ticks to a scrollable chart
     */
    LvExampleChart2() = default;
    LvExampleChart2(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        /*Create a container*/
        lv_obj_t *main_cont = lv_obj_create(parent);
        lv_obj_set_size(main_cont, 200, 150);
        lv_obj_center(main_cont);

        /*Create a transparent wrapper for the chart and the scale.
         *Set a large width, to make it scrollable on the main container*/
        lv_obj_t *wrapper = lv_obj_create(main_cont);
        lv_obj_remove_style_all(wrapper);
        lv_obj_set_size(wrapper, lv_pct(300), lv_pct(100));
        lv_obj_set_flex_flow(wrapper, LV_FLEX_FLOW_COLUMN);

        /*Create a chart on the wrapper
         *Set it's width to 100% to fill the large wrapper*/
        lv_obj_t *chart = lv_chart_create(wrapper);
        lv_obj_set_width(chart, lv_pct(100));
        lv_obj_set_flex_grow(chart, 1);
        lv_chart_set_type(chart, LV_CHART_TYPE_BAR);
        lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
        lv_chart_set_range(chart, LV_CHART_AXIS_SECONDARY_Y, 0, 400);
        lv_chart_set_point_count(chart, 12);
        lv_obj_set_style_radius(chart, 0, 0);

        /*Create a scale also with 100% width*/
        lv_obj_t *scale_bottom = lv_scale_create(wrapper);
        lv_scale_set_mode(scale_bottom, LV_SCALE_MODE_HORIZONTAL_BOTTOM);
        lv_obj_set_size(scale_bottom, lv_pct(100), 25);
        lv_scale_set_total_tick_count(scale_bottom, 12);
        lv_scale_set_major_tick_every(scale_bottom, 1);
        lv_obj_set_style_pad_hor(scale_bottom, lv_chart_get_first_point_center_offset(chart), 0);

        static const char *month[] = {"Jan", "Febr", "March", "Apr", "May", "Jun", "July",
                                      "Aug", "Sept", "Oct",   "Nov", "Dec", NULL};
        lv_scale_set_text_src(scale_bottom, month);

        /*Add two data series*/
        lv_chart_series_t *ser1 =
            lv_chart_add_series(chart, lv_palette_lighten(LV_PALETTE_GREEN, 2), LV_CHART_AXIS_PRIMARY_Y);
        lv_chart_series_t *ser2 =
            lv_chart_add_series(chart, lv_palette_darken(LV_PALETTE_GREEN, 2), LV_CHART_AXIS_PRIMARY_Y);

        /*Set the next points on 'ser1'*/
        uint32_t i;
        for (i = 0; i < 12; i++) {
            lv_chart_set_next_value(chart, ser1, lv_rand(10, 60));
            lv_chart_set_next_value(chart, ser2, lv_rand(50, 90));
        }
        lv_chart_refresh(chart); /*Required after direct set*/
    }
};

class LvExampleChart3 : public LvglComponensBase {
public:
    static void event_cb(lv_event_t *e)
    {
        lv_event_code_t code = lv_event_get_code(e);
        lv_obj_t *chart      = lv_event_get_target(e);

        if (code == LV_EVENT_VALUE_CHANGED) {
            lv_obj_invalidate(chart);
        }
        if (code == LV_EVENT_REFR_EXT_DRAW_SIZE) {
            int32_t *s = lv_event_get_param(e);
            *s         = LV_MAX(*s, 20);
        } else if (code == LV_EVENT_DRAW_POST_END) {
            int32_t id = lv_chart_get_pressed_point(chart);
            if (id == LV_CHART_POINT_NONE) return;

            LV_LOG_USER("Selected point %d", (int)id);

            lv_chart_series_t *ser = lv_chart_get_series_next(chart, NULL);
            while (ser) {
                lv_point_t p;
                lv_chart_get_point_pos_by_id(chart, ser, id, &p);

                int32_t *y_array = lv_chart_get_y_array(chart, ser);
                int32_t value    = y_array[id];

                /*Draw a rectangle above the clicked point*/
                lv_layer_t *layer = lv_event_get_layer(e);
                lv_draw_rect_dsc_t draw_rect_dsc;
                lv_draw_rect_dsc_init(&draw_rect_dsc);
                draw_rect_dsc.bg_color = lv_color_black();
                draw_rect_dsc.bg_opa   = LV_OPA_50;
                draw_rect_dsc.radius   = 3;

                lv_area_t chart_obj_coords;
                lv_obj_get_coords(chart, &chart_obj_coords);
                lv_area_t rect_area;
                rect_area.x1 = chart_obj_coords.x1 + p.x - 20;
                rect_area.x2 = chart_obj_coords.x1 + p.x + 20;
                rect_area.y1 = chart_obj_coords.y1 + p.y - 30;
                rect_area.y2 = chart_obj_coords.y1 + p.y - 10;
                lv_draw_rect(layer, &draw_rect_dsc, &rect_area);

                /*Draw the value as label to the center of the rectangle*/
                char buf[16];
                lv_snprintf(buf, sizeof(buf), LV_SYMBOL_DUMMY "$%d", value);

                lv_draw_label_dsc_t draw_label_dsc;
                lv_draw_label_dsc_init(&draw_label_dsc);
                draw_label_dsc.color      = lv_color_white();
                draw_label_dsc.text       = buf;
                draw_label_dsc.text_local = 1;
                draw_label_dsc.align      = LV_TEXT_ALIGN_CENTER;
                lv_area_t label_area      = rect_area;
                lv_area_set_height(&label_area, lv_font_get_line_height(draw_label_dsc.font));
                lv_area_align(&rect_area, &label_area, LV_ALIGN_CENTER, 0, 0);
                lv_draw_label(layer, &draw_label_dsc, &label_area);

                ser = lv_chart_get_series_next(chart, ser);
            }
        } else if (code == LV_EVENT_RELEASED) {
            lv_obj_invalidate(chart);
        }
    }

    /**
     * Show the value of the pressed points
     */
    LvExampleChart3() = default;
    LvExampleChart3(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        /*Create a chart*/
        lv_obj_t *chart;
        chart = lv_chart_create(parent);
        lv_obj_set_size(chart, 200, 150);
        lv_obj_center(chart);

        lv_obj_add_event_cb(chart, event_cb, LV_EVENT_ALL, NULL);
        lv_obj_refresh_ext_draw_size(chart);

        /*Zoom in a little in X*/
        //    lv_chart_set_scale_x(chart, 800);

        /*Add two data series*/
        lv_chart_series_t *ser1 = lv_chart_add_series(chart, lv_palette_main(LV_PALETTE_RED), LV_CHART_AXIS_PRIMARY_Y);
        lv_chart_series_t *ser2 =
            lv_chart_add_series(chart, lv_palette_main(LV_PALETTE_GREEN), LV_CHART_AXIS_PRIMARY_Y);
        uint32_t i;
        for (i = 0; i < 10; i++) {
            lv_chart_set_next_value(chart, ser1, lv_rand(60, 90));
            lv_chart_set_next_value(chart, ser2, lv_rand(10, 40));
        }
    }
};

class LvExampleChart4 : public LvglComponensBase {
public:
    static void draw_event_cb(lv_event_t *e)
    {
        lv_draw_task_t *draw_task    = lv_event_get_draw_task(e);
        lv_draw_dsc_base_t *base_dsc = lv_draw_task_get_draw_dsc(draw_task);

        if (base_dsc->part != LV_PART_ITEMS) {
            return;
        }

        lv_draw_fill_dsc_t *fill_dsc = lv_draw_task_get_fill_dsc(draw_task);
        if (fill_dsc) {
            lv_obj_t *chart  = lv_event_get_target(e);
            int32_t *y_array = lv_chart_get_y_array(chart, lv_chart_get_series_next(chart, NULL));
            int32_t v        = y_array[base_dsc->id2];

            uint32_t ratio  = v * 255 / 100;
            fill_dsc->color = lv_color_mix(lv_palette_main(LV_PALETTE_GREEN), lv_palette_main(LV_PALETTE_RED), ratio);
        }
    }

    /**
     * Recolor the bars of a chart based on their value
     */
    LvExampleChart4() = default;
    LvExampleChart4(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        /*Create a chart1*/
        lv_obj_t *chart = lv_chart_create(parent);
        lv_chart_set_type(chart, LV_CHART_TYPE_BAR);
        lv_chart_set_point_count(chart, 24);
        lv_obj_set_style_pad_column(chart, 2, 0);
        lv_obj_set_size(chart, 260, 160);
        lv_obj_center(chart);

        lv_chart_series_t *ser = lv_chart_add_series(chart, lv_color_hex(0xff0000), LV_CHART_AXIS_PRIMARY_Y);
        lv_obj_add_event_cb(chart, draw_event_cb, LV_EVENT_DRAW_TASK_ADDED, NULL);
        lv_obj_add_flag(chart, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);

        uint32_t i;
        for (i = 0; i < 24; i++) {
            lv_chart_set_next_value(chart, ser, lv_rand(10, 90));
        }
    }
};

class LvExampleChart5 : public LvglComponensBase {
public:
    static void hook_division_lines(lv_event_t *e);
    static void add_faded_area(lv_event_t *e);
    static void draw_event_cb(lv_event_t *e);

    /**
     * Add a faded area effect to the line chart and make some division lines ticker
     */
    LvExampleChart5() = default;
    LvExampleChart5(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        /*Create a chart*/
        lv_obj_t *chart = lv_chart_create(parent);
        lv_chart_set_type(chart, LV_CHART_TYPE_LINE); /*Show lines and points too*/
        lv_obj_set_size(chart, 200, 150);
        lv_obj_set_style_pad_all(chart, 0, 0);
        lv_obj_set_style_radius(chart, 0, 0);
        lv_obj_center(chart);

        lv_chart_set_div_line_count(chart, 5, 7);

        lv_obj_add_event_cb(chart, draw_event_cb, LV_EVENT_DRAW_TASK_ADDED, NULL);
        lv_obj_add_flag(chart, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);

        lv_chart_series_t *ser = lv_chart_add_series(chart, lv_palette_main(LV_PALETTE_RED), LV_CHART_AXIS_PRIMARY_Y);

        uint32_t i;
        for (i = 0; i < 10; i++) {
            lv_chart_set_next_value(chart, ser, lv_rand(10, 80));
        }
    }

    static void draw_event_cb(lv_event_t *e)
    {
        lv_draw_task_t *draw_task    = lv_event_get_draw_task(e);
        lv_draw_dsc_base_t *base_dsc = lv_draw_task_get_draw_dsc(draw_task);

        if (base_dsc->part == LV_PART_ITEMS && lv_draw_task_get_type(draw_task) == LV_DRAW_TASK_TYPE_LINE) {
            add_faded_area(e);
        }
        /*Hook the division lines too*/
        if (base_dsc->part == LV_PART_MAIN && lv_draw_task_get_type(draw_task) == LV_DRAW_TASK_TYPE_LINE) {
            hook_division_lines(e);
        }
    }

    static void add_faded_area(lv_event_t *e)
    {
        lv_obj_t *obj = lv_event_get_target(e);
        lv_area_t coords;
        lv_obj_get_coords(obj, &coords);

        lv_draw_task_t *draw_task    = lv_event_get_draw_task(e);
        lv_draw_dsc_base_t *base_dsc = lv_draw_task_get_draw_dsc(draw_task);

        const lv_chart_series_t *ser = lv_chart_get_series_next(obj, NULL);
        lv_color_t ser_color         = lv_chart_get_series_color(obj, ser);

        /*Draw a triangle below the line witch some opacity gradient*/
        lv_draw_line_dsc_t *draw_line_dsc = lv_draw_task_get_draw_dsc(draw_task);
        lv_draw_triangle_dsc_t tri_dsc;

        lv_draw_triangle_dsc_init(&tri_dsc);
        tri_dsc.p[0].x      = draw_line_dsc->p1.x;
        tri_dsc.p[0].y      = draw_line_dsc->p1.y;
        tri_dsc.p[1].x      = draw_line_dsc->p2.x;
        tri_dsc.p[1].y      = draw_line_dsc->p2.y;
        tri_dsc.p[2].x      = draw_line_dsc->p1.y < draw_line_dsc->p2.y ? draw_line_dsc->p1.x : draw_line_dsc->p2.x;
        tri_dsc.p[2].y      = LV_MAX(draw_line_dsc->p1.y, draw_line_dsc->p2.y);
        tri_dsc.bg_grad.dir = LV_GRAD_DIR_VER;

        int32_t full_h       = lv_obj_get_height(obj);
        int32_t fract_uppter = (int32_t)(LV_MIN(draw_line_dsc->p1.y, draw_line_dsc->p2.y) - coords.y1) * 255 / full_h;
        int32_t fract_lower  = (int32_t)(LV_MAX(draw_line_dsc->p1.y, draw_line_dsc->p2.y) - coords.y1) * 255 / full_h;
        tri_dsc.bg_grad.stops[0].color = ser_color;
        tri_dsc.bg_grad.stops[0].opa   = 255 - fract_uppter;
        tri_dsc.bg_grad.stops[0].frac  = 0;
        tri_dsc.bg_grad.stops[1].color = ser_color;
        tri_dsc.bg_grad.stops[1].opa   = 255 - fract_lower;
        tri_dsc.bg_grad.stops[1].frac  = 255;

        lv_draw_triangle(base_dsc->layer, &tri_dsc);

        /*Draw rectangle below the triangle*/
        lv_draw_rect_dsc_t rect_dsc;
        lv_draw_rect_dsc_init(&rect_dsc);
        rect_dsc.bg_grad.dir            = LV_GRAD_DIR_VER;
        rect_dsc.bg_grad.stops[0].color = ser_color;
        rect_dsc.bg_grad.stops[0].frac  = 0;
        rect_dsc.bg_grad.stops[0].opa   = 255 - fract_lower;
        rect_dsc.bg_grad.stops[1].color = ser_color;
        rect_dsc.bg_grad.stops[1].frac  = 255;
        rect_dsc.bg_grad.stops[1].opa   = 0;

        lv_area_t rect_area;
        rect_area.x1 = (int32_t)draw_line_dsc->p1.x;
        rect_area.x2 = (int32_t)draw_line_dsc->p2.x - 1;
        rect_area.y1 = (int32_t)LV_MAX(draw_line_dsc->p1.y, draw_line_dsc->p2.y) - 1;
        rect_area.y2 = (int32_t)coords.y2;
        lv_draw_rect(base_dsc->layer, &rect_dsc, &rect_area);
    }

    static void hook_division_lines(lv_event_t *e)
    {
        lv_draw_task_t *draw_task    = lv_event_get_draw_task(e);
        lv_draw_dsc_base_t *base_dsc = lv_draw_task_get_draw_dsc(draw_task);
        lv_draw_line_dsc_t *line_dsc = lv_draw_task_get_draw_dsc(draw_task);

        /*Vertical line*/
        if (line_dsc->p1.x == line_dsc->p2.x) {
            line_dsc->color = lv_palette_lighten(LV_PALETTE_GREY, 1);
            if (base_dsc->id1 == 3) {
                line_dsc->width      = 2;
                line_dsc->dash_gap   = 0;
                line_dsc->dash_width = 0;
            } else {
                line_dsc->width      = 1;
                line_dsc->dash_gap   = 6;
                line_dsc->dash_width = 6;
            }
        }
        /*Horizontal line*/
        else {
            if (base_dsc->id1 == 2) {
                line_dsc->width      = 2;
                line_dsc->dash_gap   = 0;
                line_dsc->dash_width = 0;
            } else {
                line_dsc->width      = 2;
                line_dsc->dash_gap   = 6;
                line_dsc->dash_width = 6;
            }

            if (base_dsc->id1 == 1 || base_dsc->id1 == 3) {
                line_dsc->color = lv_palette_main(LV_PALETTE_GREEN);
            } else {
                line_dsc->color = lv_palette_lighten(LV_PALETTE_GREY, 1);
            }
        }
    }
};

class LvExampleChart6 : public LvglComponensBase {
public:
    inline static lv_obj_t *chart;
    inline static lv_chart_series_t *ser;
    inline static lv_chart_cursor_t *cursor;

    static void value_changed_event_cb(lv_event_t *e)
    {
        static int32_t last_id = -1;
        lv_obj_t *obj          = lv_event_get_target(e);

        last_id = lv_chart_get_pressed_point(obj);
        if (last_id != LV_CHART_POINT_NONE) {
            lv_chart_set_cursor_point(obj, cursor, NULL, last_id);
        }
    }

    /**
     * Show cursor on the clicked point
     */
    LvExampleChart6() = default;
    LvExampleChart6(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        chart = lv_chart_create(parent);
        lv_obj_set_size(chart, 200, 150);
        lv_obj_align(chart, LV_ALIGN_CENTER, 0, -10);

        //    lv_chart_set_axis_tick(chart, LV_CHART_AXIS_PRIMARY_Y, 10, 5, 6, 5, true, 40);
        //    lv_chart_set_axis_tick(chart, LV_CHART_AXIS_PRIMARY_X, 10, 5, 10, 1, true, 30);

        lv_obj_add_event_cb(chart, value_changed_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
        lv_obj_refresh_ext_draw_size(chart);

        cursor = lv_chart_add_cursor(chart, lv_palette_main(LV_PALETTE_BLUE), LV_DIR_LEFT | LV_DIR_BOTTOM);

        ser = lv_chart_add_series(chart, lv_palette_main(LV_PALETTE_RED), LV_CHART_AXIS_PRIMARY_Y);
        uint32_t i;
        for (i = 0; i < 10; i++) {
            lv_chart_set_next_value(chart, ser, lv_rand(10, 90));
        }

        //    lv_chart_set_scale_x(chart, 500);

        lv_obj_t *label = lv_label_create(parent);
        lv_label_set_text(label, "Click on a point");
        lv_obj_align_to(label, chart, LV_ALIGN_OUT_TOP_MID, 0, -5);
    }
};

class LvExampleChart7 : public LvglComponensBase {
public:
    static void draw_event_cb(lv_event_t *e)
    {
        lv_draw_task_t *draw_task    = lv_event_get_draw_task(e);
        lv_draw_dsc_base_t *base_dsc = lv_draw_task_get_draw_dsc(draw_task);
        if (base_dsc->part == LV_PART_INDICATOR) {
            lv_obj_t *obj                     = lv_event_get_target(e);
            lv_chart_series_t *ser            = lv_chart_get_series_next(obj, NULL);
            lv_draw_rect_dsc_t *rect_draw_dsc = lv_draw_task_get_draw_dsc(draw_task);
            uint32_t cnt                      = lv_chart_get_point_count(obj);

            /*Make older value more transparent*/
            rect_draw_dsc->bg_opa = (LV_OPA_COVER * base_dsc->id2) / (cnt - 1);

            /*Make smaller values blue, higher values red*/
            int32_t *x_array = lv_chart_get_x_array(obj, ser);
            int32_t *y_array = lv_chart_get_y_array(obj, ser);
            /*dsc->id is the tells drawing order, but we need the ID of the point being drawn.*/
            uint32_t start_point = lv_chart_get_x_start_point(obj, ser);
            uint32_t p_act = (start_point + base_dsc->id2) % cnt; /*Consider start point to get the index of the array*/
            lv_opa_t x_opa = (x_array[p_act] * LV_OPA_50) / 200;
            lv_opa_t y_opa = (y_array[p_act] * LV_OPA_50) / 1000;

            rect_draw_dsc->bg_color =
                lv_color_mix(lv_palette_main(LV_PALETTE_RED), lv_palette_main(LV_PALETTE_BLUE), x_opa + y_opa);
        }
    }

    static void add_data(lv_timer_t *timer)
    {
        lv_obj_t *chart = lv_timer_get_user_data(timer);
        lv_chart_set_next_value2(chart, lv_chart_get_series_next(chart, NULL), lv_rand(0, 200), lv_rand(0, 1000));
    }

    /**
     * A scatter chart
     */
    LvExampleChart7() = default;
    LvExampleChart7(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        lv_obj_t *chart = lv_chart_create(parent);
        lv_obj_set_size(chart, 200, 150);
        lv_obj_align(chart, LV_ALIGN_CENTER, 0, 0);
        lv_obj_add_event_cb(chart, draw_event_cb, LV_EVENT_DRAW_TASK_ADDED, NULL);
        lv_obj_add_flag(chart, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);
        lv_obj_set_style_line_width(chart, 0, LV_PART_ITEMS); /*Remove the lines*/

        lv_chart_set_type(chart, LV_CHART_TYPE_SCATTER);

        lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_X, 0, 200);
        lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, 1000);

        lv_chart_set_point_count(chart, 50);

        lv_chart_series_t *ser = lv_chart_add_series(chart, lv_palette_main(LV_PALETTE_RED), LV_CHART_AXIS_PRIMARY_Y);
        uint32_t i;
        for (i = 0; i < 50; i++) {
            lv_chart_set_next_value2(chart, ser, lv_rand(0, 200), lv_rand(0, 1000));
        }

        lv_timer_create(add_data, 100, chart);
    }
};

class LvExampleChart8 : public LvglComponensBase {
public:
    static void add_data(lv_timer_t *t)
    {
        lv_obj_t *chart        = lv_timer_get_user_data(t);
        lv_chart_series_t *ser = lv_chart_get_series_next(chart, NULL);

        lv_chart_set_next_value(chart, ser, lv_rand(10, 90));

        uint16_t p = lv_chart_get_point_count(chart);
        uint16_t s = lv_chart_get_x_start_point(chart, ser);
        int32_t *a = lv_chart_get_y_array(chart, ser);

        a[(s + 1) % p] = LV_CHART_POINT_NONE;
        a[(s + 2) % p] = LV_CHART_POINT_NONE;
        a[(s + 2) % p] = LV_CHART_POINT_NONE;

        lv_chart_refresh(chart);
    }

    /**
     * Circular line chart with gap
     */
    LvExampleChart8() = default;
    LvExampleChart8(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        /*Create a stacked_area_chart.obj*/
        lv_obj_t *chart = lv_chart_create(parent);
        lv_chart_set_update_mode(chart, LV_CHART_UPDATE_MODE_CIRCULAR);
        lv_obj_set_style_size(chart, 0, 0, LV_PART_INDICATOR);
        lv_obj_set_size(chart, 280, 150);
        lv_obj_center(chart);

        lv_chart_set_point_count(chart, 80);
        lv_chart_series_t *ser = lv_chart_add_series(chart, lv_palette_main(LV_PALETTE_RED), LV_CHART_AXIS_PRIMARY_Y);
        /*Prefill with data*/
        uint32_t i;
        for (i = 0; i < 80; i++) {
            lv_chart_set_next_value(chart, ser, lv_rand(10, 90));
        }

        lv_timer_create(add_data, 300, chart);
    }
};

class LvExampleCheckbox1 : public LvglComponensBase {
public:
    static void event_handler(lv_event_t *e)
    {
        lv_event_code_t code = lv_event_get_code(e);
        lv_obj_t *obj        = lv_event_get_target(e);
        if (code == LV_EVENT_VALUE_CHANGED) {
            LV_UNUSED(obj);
            const char *txt   = lv_checkbox_get_text(obj);
            const char *state = lv_obj_get_state(obj) & LV_STATE_CHECKED ? "Checked" : "Unchecked";
            LV_UNUSED(txt);
            LV_UNUSED(state);
            LV_LOG_USER("%s: %s", txt, state);
        }
    }

    LvExampleCheckbox1() = default;
    LvExampleCheckbox1(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);

        lv_obj_t *cb;
        cb = lv_checkbox_create(parent);
        lv_checkbox_set_text(cb, "Apple");
        lv_obj_add_event_cb(cb, event_handler, LV_EVENT_ALL, NULL);

        cb = lv_checkbox_create(parent);
        lv_checkbox_set_text(cb, "Banana");
        lv_obj_add_state(cb, LV_STATE_CHECKED);
        lv_obj_add_event_cb(cb, event_handler, LV_EVENT_ALL, NULL);

        cb = lv_checkbox_create(parent);
        lv_checkbox_set_text(cb, "Lemon");
        lv_obj_add_state(cb, LV_STATE_DISABLED);
        lv_obj_add_event_cb(cb, event_handler, LV_EVENT_ALL, NULL);

        cb = lv_checkbox_create(parent);
        lv_obj_add_state(cb, LV_STATE_CHECKED | LV_STATE_DISABLED);
        lv_checkbox_set_text(cb, "Melon\nand a new line");
        lv_obj_add_event_cb(cb, event_handler, LV_EVENT_ALL, NULL);

        lv_obj_update_layout(cb);
    }
};

class LvExampleCheckbox2 : public LvglComponensBase {
public:
    inline static lv_style_t style_radio;
    inline static lv_style_t style_radio_chk;
    inline static uint32_t active_index_1 = 0;
    inline static uint32_t active_index_2 = 0;

    static void radio_event_handler(lv_event_t *e)
    {
        uint32_t *active_id = lv_event_get_user_data(e);
        lv_obj_t *cont      = lv_event_get_current_target(e);
        lv_obj_t *act_cb    = lv_event_get_target(e);
        lv_obj_t *old_cb    = lv_obj_get_child(cont, *active_id);

        /*Do nothing if the container was clicked*/
        if (act_cb == cont) return;

        lv_obj_remove_state(old_cb, LV_STATE_CHECKED); /*Uncheck the previous radio button*/
        lv_obj_add_state(act_cb, LV_STATE_CHECKED);    /*Uncheck the current radio button*/

        *active_id = lv_obj_get_index(act_cb);

        LV_LOG_USER("Selected radio buttons: %d, %d", (int)active_index_1, (int)active_index_2);
    }

    static void radiobutton_create(lv_obj_t *parent, const char *txt)
    {
        lv_obj_t *obj = lv_checkbox_create(parent);
        lv_checkbox_set_text(obj, txt);
        lv_obj_add_flag(obj, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_add_style(obj, &style_radio, LV_PART_INDICATOR);
        lv_obj_add_style(obj, &style_radio_chk, LV_PART_INDICATOR | LV_STATE_CHECKED);
    }

    /**
     * Checkboxes as radio buttons
     */
    LvExampleCheckbox2() = default;
    LvExampleCheckbox2(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        /* The idea is to enable `LV_OBJ_FLAG_EVENT_BUBBLE` on checkboxes and process the
         * `LV_EVENT_CLICKED` on the container.
         * A variable is passed as event user data where the index of the active
         * radiobutton is saved */

        lv_style_init(&style_radio);
        lv_style_set_radius(&style_radio, LV_RADIUS_CIRCLE);

        lv_style_init(&style_radio_chk);
        lv_style_set_bg_image_src(&style_radio_chk, NULL);

        uint32_t i;
        char buf[32];

        lv_obj_t *cont1 = lv_obj_create(parent);
        lv_obj_set_flex_flow(cont1, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_size(cont1, lv_pct(40), lv_pct(80));
        lv_obj_add_event_cb(cont1, radio_event_handler, LV_EVENT_CLICKED, &active_index_1);

        for (i = 0; i < 5; i++) {
            lv_snprintf(buf, sizeof(buf), "A %d", (int)i + 1);
            radiobutton_create(cont1, buf);
        }
        /*Make the first checkbox checked*/
        lv_obj_add_state(lv_obj_get_child(cont1, 0), LV_STATE_CHECKED);

        lv_obj_t *cont2 = lv_obj_create(parent);
        lv_obj_set_flex_flow(cont2, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_size(cont2, lv_pct(40), lv_pct(80));
        lv_obj_set_x(cont2, lv_pct(50));
        lv_obj_add_event_cb(cont2, radio_event_handler, LV_EVENT_CLICKED, &active_index_2);

        for (i = 0; i < 3; i++) {
            lv_snprintf(buf, sizeof(buf), "B %d", (int)i + 1);
            radiobutton_create(cont2, buf);
        }

        /*Make the first checkbox checked*/
        lv_obj_add_state(lv_obj_get_child(cont2, 0), LV_STATE_CHECKED);
    }
};

class LvExampleDropdown1 : public LvglComponensBase {
public:
    static void event_handler(lv_event_t *e)
    {
        lv_event_code_t code = lv_event_get_code(e);
        lv_obj_t *obj        = lv_event_get_target(e);
        if (code == LV_EVENT_VALUE_CHANGED) {
            char buf[32];
            lv_dropdown_get_selected_str(obj, buf, sizeof(buf));
            LV_LOG_USER("Option: %s", buf);
        }
    }

    LvExampleDropdown1() = default;
    LvExampleDropdown1(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        /*Create a normal drop down list*/
        lv_obj_t *dd = lv_dropdown_create(parent);
        lv_dropdown_set_options(dd,
                                "Apple\n"
                                "Banana\n"
                                "Orange\n"
                                "Cherry\n"
                                "Grape\n"
                                "Raspberry\n"
                                "Melon\n"
                                "Orange\n"
                                "Lemon\n"
                                "Nuts");

        lv_obj_align(dd, LV_ALIGN_TOP_MID, 0, 20);
        lv_obj_add_event_cb(dd, event_handler, LV_EVENT_ALL, NULL);
    }
};

class LvExampleDropdown2 : public LvglComponensBase {
public:
    /**
     * Create a drop down, up, left and right menus
     */
    LvExampleDropdown2() = default;
    LvExampleDropdown2(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        static const char *opts =
            "Apple\n"
            "Banana\n"
            "Orange\n"
            "Melon";

        lv_obj_t *dd;
        dd = lv_dropdown_create(parent);
        lv_dropdown_set_options_static(dd, opts);
        lv_obj_align(dd, LV_ALIGN_TOP_MID, 0, 10);

        dd = lv_dropdown_create(parent);
        lv_dropdown_set_options_static(dd, opts);
        lv_dropdown_set_dir(dd, LV_DIR_BOTTOM);
        lv_dropdown_set_symbol(dd, LV_SYMBOL_UP);
        lv_obj_align(dd, LV_ALIGN_BOTTOM_MID, 0, -10);

        dd = lv_dropdown_create(parent);
        lv_dropdown_set_options_static(dd, opts);
        lv_dropdown_set_dir(dd, LV_DIR_RIGHT);
        lv_dropdown_set_symbol(dd, LV_SYMBOL_RIGHT);
        lv_obj_align(dd, LV_ALIGN_LEFT_MID, 10, 0);

        dd = lv_dropdown_create(parent);
        lv_dropdown_set_options_static(dd, opts);
        lv_dropdown_set_dir(dd, LV_DIR_LEFT);
        lv_dropdown_set_symbol(dd, LV_SYMBOL_LEFT);
        lv_obj_align(dd, LV_ALIGN_RIGHT_MID, -10, 0);
    }
};

class LvExampleDropdown3 : public LvglComponensBase {
public:
    static void event_cb(lv_event_t *e)
    {
        lv_obj_t *dropdown = lv_event_get_target(e);
        char buf[64];
        lv_dropdown_get_selected_str(dropdown, buf, sizeof(buf));
        LV_LOG_USER("'%s' is selected", buf);
    }

    /**
     * Create a menu from a drop-down list and show some drop-down list features and styling
     */
    LvExampleDropdown3() = default;
    LvExampleDropdown3(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        /*Create a drop down list*/
        lv_obj_t *dropdown = lv_dropdown_create(parent);
        lv_obj_align(dropdown, LV_ALIGN_TOP_LEFT, 10, 10);
        lv_dropdown_set_options(dropdown,
                                "New project\n"
                                "New file\n"
                                "Save\n"
                                "Save as ...\n"
                                "Open project\n"
                                "Recent projects\n"
                                "Preferences\n"
                                "Exit");

        /*Set a fixed text to display on the button of the drop-down list*/
        lv_dropdown_set_text(dropdown, "Menu");

        /*Use a custom image as down icon and flip it when the list is opened*/
        LV_IMAGE_DECLARE(img_caret_down);
        lv_dropdown_set_symbol(dropdown, &img_caret_down);
        lv_obj_set_style_transform_rotation(dropdown, 1800, LV_PART_INDICATOR | LV_STATE_CHECKED);

        /*In a menu we don't need to show the last clicked item*/
        lv_dropdown_set_selected_highlight(dropdown, false);

        lv_obj_add_event_cb(dropdown, event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    }
};

class LvExampleImagebutton1 : public LvglComponensBase {
public:
    LvExampleImagebutton1() = default;
    LvExampleImagebutton1(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        LV_IMAGE_DECLARE(imagebutton_left);
        LV_IMAGE_DECLARE(imagebutton_right);
        LV_IMAGE_DECLARE(imagebutton_mid);

        /*Create a transition animation on width transformation and recolor.*/
        static lv_style_prop_t tr_prop[] = {LV_STYLE_TRANSFORM_WIDTH, LV_STYLE_IMAGE_RECOLOR_OPA, 0};
        static lv_style_transition_dsc_t tr;
        lv_style_transition_dsc_init(&tr, tr_prop, lv_anim_path_linear, 200, 0, NULL);

        static lv_style_t style_def;
        lv_style_init(&style_def);
        lv_style_set_text_color(&style_def, lv_color_white());
        lv_style_set_transition(&style_def, &tr);

        /*Darken the button when pressed and make it wider*/
        static lv_style_t style_pr;
        lv_style_init(&style_pr);
        lv_style_set_image_recolor_opa(&style_pr, LV_OPA_30);
        lv_style_set_image_recolor(&style_pr, lv_color_black());
        lv_style_set_transform_width(&style_pr, 20);

        /*Create an image button*/
        lv_obj_t *imagebutton1 = lv_imagebutton_create(parent);
        lv_imagebutton_set_src(imagebutton1, LV_IMAGEBUTTON_STATE_RELEASED, &imagebutton_left, &imagebutton_mid,
                               &imagebutton_right);
        lv_obj_add_style(imagebutton1, &style_def, 0);
        lv_obj_add_style(imagebutton1, &style_pr, LV_STATE_PRESSED);

        lv_obj_set_width(imagebutton1, 100);
        lv_obj_align(imagebutton1, LV_ALIGN_CENTER, 0, 0);

        /*Create a label on the image button*/
        lv_obj_t *label = lv_label_create(imagebutton1);
        lv_label_set_text(label, "Button");
        lv_obj_align(label, LV_ALIGN_CENTER, 0, -4);
    }
};

class LvExampleKeyboard1 : public LvglComponensBase {
public:
    static void ta_event_cb(lv_event_t *e)
    {
        lv_event_code_t code = lv_event_get_code(e);
        lv_obj_t *ta         = lv_event_get_target(e);
        lv_obj_t *kb         = lv_event_get_user_data(e);
        if (code == LV_EVENT_FOCUSED) {
            lv_keyboard_set_textarea(kb, ta);
            lv_obj_remove_flag(kb, LV_OBJ_FLAG_HIDDEN);
        }

        if (code == LV_EVENT_DEFOCUSED) {
            lv_keyboard_set_textarea(kb, NULL);
            lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
        }
    }

    LvExampleKeyboard1() = default;
    LvExampleKeyboard1(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        /*Create a keyboard to use it with an of the text areas*/
        lv_obj_t *kb = lv_keyboard_create(parent);

        /*Create a text area. The keyboard will write here*/
        lv_obj_t *ta1;
        ta1 = lv_textarea_create(parent);
        lv_obj_align(ta1, LV_ALIGN_TOP_LEFT, 10, 10);
        lv_obj_add_event_cb(ta1, ta_event_cb, LV_EVENT_ALL, kb);
        lv_textarea_set_placeholder_text(ta1, "Hello");
        lv_obj_set_size(ta1, 140, 80);

        lv_obj_t *ta2;
        ta2 = lv_textarea_create(parent);
        lv_obj_align(ta2, LV_ALIGN_TOP_RIGHT, -10, 10);
        lv_obj_add_event_cb(ta2, ta_event_cb, LV_EVENT_ALL, kb);
        lv_obj_set_size(ta2, 140, 80);

        lv_keyboard_set_textarea(kb, ta1);

        /*The keyboard will show Arabic characters if they are enabled */
#if LV_USE_ARABIC_PERSIAN_CHARS && LV_FONT_DEJAVU_16_PERSIAN_HEBREW
        lv_obj_set_style_text_font(kb, &lv_font_dejavu_16_persian_hebrew, 0);
        lv_obj_set_style_text_font(ta1, &lv_font_dejavu_16_persian_hebrew, 0);
        lv_obj_set_style_text_font(ta2, &lv_font_dejavu_16_persian_hebrew, 0);
#endif
    }
};

class LvExampleKeyboard2 : public LvglComponensBase {
public:
    LvExampleKeyboard2() = default;
    LvExampleKeyboard2(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        /*Create an AZERTY keyboard map*/
        static const char *kb_map[] = {"A",
                                       "Z",
                                       "E",
                                       "R",
                                       "T",
                                       "Y",
                                       "U",
                                       "I",
                                       "O",
                                       "P",
                                       LV_SYMBOL_BACKSPACE,
                                       "\n",
                                       "Q",
                                       "S",
                                       "D",
                                       "F",
                                       "G",
                                       "J",
                                       "K",
                                       "L",
                                       "M",
                                       LV_SYMBOL_NEW_LINE,
                                       "\n",
                                       "W",
                                       "X",
                                       "C",
                                       "V",
                                       "B",
                                       "N",
                                       ",",
                                       ".",
                                       ":",
                                       "!",
                                       "?",
                                       "\n",
                                       LV_SYMBOL_CLOSE,
                                       " ",
                                       " ",
                                       " ",
                                       LV_SYMBOL_OK,
                                       NULL};

        /*Set the relative width of the buttons and other controls*/
        static const lv_buttonmatrix_ctrl_t kb_ctrl[] = {4, 4,
                                                         4, 4,
                                                         4, 4,
                                                         4, 4,
                                                         4, 4,
                                                         6, 4,
                                                         4, 4,
                                                         4, 4,
                                                         4, 4,
                                                         4, 4,
                                                         6, 4,
                                                         4, 4,
                                                         4, 4,
                                                         4, 4,
                                                         4, 4,
                                                         4, 4,
                                                         2, LV_BUTTONMATRIX_CTRL_HIDDEN | 2,
                                                         6, LV_BUTTONMATRIX_CTRL_HIDDEN | 2,
                                                         2};

        /*Create a keyboard and add the new map as USER_1 mode*/
        lv_obj_t *kb = lv_keyboard_create(parent);

        lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_USER_1, kb_map, kb_ctrl);
        lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_USER_1);

        /*Create a text area. The keyboard will write here*/
        lv_obj_t *ta;
        ta = lv_textarea_create(parent);
        lv_obj_align(ta, LV_ALIGN_TOP_MID, 0, 10);
        lv_obj_set_size(ta, lv_pct(90), 80);
        lv_obj_add_state(ta, LV_STATE_FOCUSED);

        lv_keyboard_set_textarea(kb, ta);
    }
};

class LvExampleLabel1 : public LvglComponensBase {
public:
    /**
     * Show line wrap, re-color, line align and text scrolling.
     */
    LvExampleLabel1() = default;
    LvExampleLabel1(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        lv_obj_t *label1 = lv_label_create(parent);
        lv_label_set_long_mode(label1, LV_LABEL_LONG_WRAP); /*Break the long lines*/
        lv_label_set_text(label1, "Recolor is not supported for v9 now.");
        lv_obj_set_width(label1, 150); /*Set smaller width to make the lines wrap*/
        lv_obj_set_style_text_align(label1, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(label1, LV_ALIGN_CENTER, 0, -40);

        lv_obj_t *label2 = lv_label_create(parent);
        lv_label_set_long_mode(label2, LV_LABEL_LONG_SCROLL_CIRCULAR); /*Circular scroll*/
        lv_obj_set_width(label2, 150);
        lv_label_set_text(label2, "It is a circularly scrolling text. ");
        lv_obj_align(label2, LV_ALIGN_CENTER, 0, 40);
    }
};

class LvExampleLabel2 : public LvglComponensBase {
public:
    /**
     * Create a fake text shadow
     */
    LvExampleLabel2() = default;
    LvExampleLabel2(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        /*Create a style for the shadow*/
        static lv_style_t style_shadow;
        lv_style_init(&style_shadow);
        lv_style_set_text_opa(&style_shadow, LV_OPA_30);
        lv_style_set_text_color(&style_shadow, lv_color_black());

        /*Create a label for the shadow first (it's in the background)*/
        lv_obj_t *shadow_label = lv_label_create(parent);
        lv_obj_add_style(shadow_label, &style_shadow, 0);

        /*Create the main label*/
        lv_obj_t *main_label = lv_label_create(parent);
        lv_label_set_text(main_label,
                          "A simple method to create\n"
                          "shadows on a text.\n"
                          "It even works with\n\n"
                          "newlines     and spaces.");

        /*Set the same text for the shadow label*/
        lv_label_set_text(shadow_label, lv_label_get_text(main_label));

        /*Position the main label*/
        lv_obj_align(main_label, LV_ALIGN_CENTER, 0, 0);

        /*Shift the second label down and to the right by 2 pixel*/
        lv_obj_align_to(shadow_label, main_label, LV_ALIGN_TOP_LEFT, 2, 2);
    }
};

class LvExampleLabel3 : public LvglComponensBase {
public:
    /**
     * Show mixed LTR, RTL and Chinese label
     */
    LvExampleLabel3() = default;
    LvExampleLabel3(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        lv_obj_t *ltr_label = lv_label_create(parent);
        lv_label_set_text(ltr_label,
                          "In modern terminology, a microcontroller is similar to a system on a chip (SoC).");
        lv_obj_set_style_text_font(ltr_label, &lv_font_montserrat_16, 0);
        lv_obj_set_width(ltr_label, 310);
        lv_obj_align(ltr_label, LV_ALIGN_TOP_LEFT, 5, 5);

        lv_obj_t *rtl_label = lv_label_create(parent);
        lv_label_set_text(rtl_label, "מעבד, או בשמו המלא יחידת עיבוד מרכזית (באנגלית: CPU - Central Processing Unit).");
        lv_obj_set_style_base_dir(rtl_label, LV_BASE_DIR_RTL, 0);
        lv_obj_set_style_text_font(rtl_label, &lv_font_dejavu_16_persian_hebrew, 0);
        lv_obj_set_width(rtl_label, 310);
        lv_obj_align(rtl_label, LV_ALIGN_LEFT_MID, 5, 0);

        lv_obj_t *cz_label = lv_label_create(parent);
        lv_label_set_text(
            cz_label,
            "嵌入式系统（Embedded System），\n是一种嵌入机械或电气系统内部、具有专一功能和实时计算性能的计算机系统。");
        lv_obj_set_style_text_font(cz_label, &lv_font_simsun_16_cjk, 0);
        lv_obj_set_width(cz_label, 310);
        lv_obj_align(cz_label, LV_ALIGN_BOTTOM_LEFT, 5, -5);
    }
};

class LvExampleLabel4 : public LvglComponensBase {
public:
#define MASK_WIDTH  150
#define MASK_HEIGHT 60

    static void generate_mask(lv_draw_buf_t *mask, int32_t w, int32_t h, const char *txt)
    {
        /*Create a "8 bit alpha" canvas and clear it*/
        lv_obj_t *canvas = lv_canvas_create(lv_screen_active());
        lv_canvas_set_draw_buf(canvas, mask);
        lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_TRANSP);

        lv_layer_t layer;
        lv_canvas_init_layer(canvas, &layer);

        /*Draw a label to the canvas. The result "image" will be used as mask*/
        lv_draw_label_dsc_t label_dsc;
        lv_draw_label_dsc_init(&label_dsc);
        label_dsc.color = lv_color_white();
        label_dsc.align = LV_TEXT_ALIGN_CENTER;
        label_dsc.text  = txt;
        label_dsc.font  = &lv_font_montserrat_24;
        lv_area_t a     = {0, 0, w - 1, h - 1};
        lv_draw_label(&layer, &label_dsc, &a);

        lv_canvas_finish_layer(canvas, &layer);

        lv_obj_delete(canvas);
    }

    /**
     * Draw label with gradient color
     */
    LvExampleLabel4() = default;
    LvExampleLabel4(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        /* Create the mask of a text by drawing it to a canvas*/
        LV_DRAW_BUF_DEFINE_STATIC(mask, MASK_WIDTH, MASK_HEIGHT, LV_COLOR_FORMAT_L8);
        LV_DRAW_BUF_INIT_STATIC(mask);

        generate_mask(&mask, MASK_WIDTH, MASK_HEIGHT, "Text with gradient");

        /* Create an object from where the text will be masked out.
         * Now it's a rectangle with a gradient but it could be an image too*/
        lv_obj_t *grad = lv_obj_create(parent);
        lv_obj_set_size(grad, MASK_WIDTH, MASK_HEIGHT);
        lv_obj_center(grad);
        lv_obj_set_style_bg_color(grad, lv_color_hex(0xff0000), 0);
        lv_obj_set_style_bg_grad_color(grad, lv_color_hex(0x0000ff), 0);
        lv_obj_set_style_bg_grad_dir(grad, LV_GRAD_DIR_HOR, 0);
        lv_obj_set_style_bitmap_mask_src(grad, &mask, 0);
    }
};

class LvExampleLabel5 : public LvglComponensBase {
public:
    /**
     * Show customizing the circular scrolling animation of a label with `LV_LABEL_LONG_SCROLL_CIRCULAR`
     * long mode.
     */
    LvExampleLabel5() = default;
    LvExampleLabel5(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        static lv_anim_t animation_template;
        static lv_style_t label_style;

        lv_anim_init(&animation_template);
        lv_anim_set_delay(&animation_template, 1000); /*Wait 1 second to start the first scroll*/
        lv_anim_set_repeat_delay(
            &animation_template,
            3000); /*Repeat the scroll 3 seconds after the label scrolls back to the initial position*/

        /*Initialize the label style with the animation template*/
        lv_style_init(&label_style);
        lv_style_set_anim(&label_style, &animation_template);

        lv_obj_t *label1 = lv_label_create(parent);
        lv_label_set_long_mode(label1, LV_LABEL_LONG_SCROLL_CIRCULAR); /*Circular scroll*/
        lv_obj_set_width(label1, 150);
        lv_label_set_text(label1, "It is a circularly scrolling text. ");
        lv_obj_align(label1, LV_ALIGN_CENTER, 0, 40);
        lv_obj_add_style(label1, &label_style, LV_STATE_DEFAULT); /*Add the style to the label*/
    }
};

class LvExampleLed1 : public LvglComponensBase {
public:
    /**
     * Create LED's with different brightness and color
     */
    LvExampleLed1() = default;
    LvExampleLed1(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        /*Create a LED and switch it OFF*/
        lv_obj_t *led1 = lv_led_create(parent);
        lv_obj_align(led1, LV_ALIGN_CENTER, -80, 0);
        lv_led_off(led1);

        /*Copy the previous LED and set a brightness*/
        lv_obj_t *led2 = lv_led_create(parent);
        lv_obj_align(led2, LV_ALIGN_CENTER, 0, 0);
        lv_led_set_brightness(led2, 150);
        lv_led_set_color(led2, lv_palette_main(LV_PALETTE_RED));

        /*Copy the previous LED and switch it ON*/
        lv_obj_t *led3 = lv_led_create(parent);
        lv_obj_align(led3, LV_ALIGN_CENTER, 80, 0);
        lv_led_on(led3);
    }
};

class LvExampleLine1 : public LvglComponensBase {
public:
    LvExampleLine1() = default;
    LvExampleLine1(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        /*Create an array for the points of the line*/
        static lv_point_precise_t line_points[] = {{5, 5}, {70, 70}, {120, 10}, {180, 60}, {240, 10}};

        /*Create style*/
        static lv_style_t style_line;
        lv_style_init(&style_line);
        lv_style_set_line_width(&style_line, 8);
        lv_style_set_line_color(&style_line, lv_palette_main(LV_PALETTE_BLUE));
        lv_style_set_line_rounded(&style_line, true);

        /*Create a line and apply the new style*/
        lv_obj_t *line1;
        line1 = lv_line_create(parent);
        lv_line_set_points(line1, line_points, 5); /*Set the points*/
        lv_obj_add_style(line1, &style_line, 0);
        lv_obj_center(line1);
    }
};

class LvExampleList1 : public LvglComponensBase {
public:
    inline static lv_obj_t *list1;

    static void event_handler(lv_event_t *e)
    {
        lv_event_code_t code = lv_event_get_code(e);
        lv_obj_t *obj        = lv_event_get_target(e);
        if (code == LV_EVENT_CLICKED) {
            LV_UNUSED(obj);
            LV_LOG_USER("Clicked: %s", lv_list_get_button_text(list1, obj));
        }
    }
    LvExampleList1() = default;
    LvExampleList1(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        /*Create a list*/
        list1 = lv_list_create(parent);
        lv_obj_set_size(list1, 180, 220);
        lv_obj_center(list1);

        /*Add buttons to the list*/
        lv_obj_t *btn;
        lv_list_add_text(list1, "File");
        btn = lv_list_add_button(list1, LV_SYMBOL_FILE, "New");
        lv_obj_add_event_cb(btn, event_handler, LV_EVENT_CLICKED, NULL);
        btn = lv_list_add_button(list1, LV_SYMBOL_DIRECTORY, "Open");
        lv_obj_add_event_cb(btn, event_handler, LV_EVENT_CLICKED, NULL);
        btn = lv_list_add_button(list1, LV_SYMBOL_SAVE, "Save");
        lv_obj_add_event_cb(btn, event_handler, LV_EVENT_CLICKED, NULL);
        btn = lv_list_add_button(list1, LV_SYMBOL_CLOSE, "Delete");
        lv_obj_add_event_cb(btn, event_handler, LV_EVENT_CLICKED, NULL);
        btn = lv_list_add_button(list1, LV_SYMBOL_EDIT, "Edit");
        lv_obj_add_event_cb(btn, event_handler, LV_EVENT_CLICKED, NULL);

        lv_list_add_text(list1, "Connectivity");
        btn = lv_list_add_button(list1, LV_SYMBOL_BLUETOOTH, "Bluetooth");
        lv_obj_add_event_cb(btn, event_handler, LV_EVENT_CLICKED, NULL);
        btn = lv_list_add_button(list1, LV_SYMBOL_GPS, "Navigation");
        lv_obj_add_event_cb(btn, event_handler, LV_EVENT_CLICKED, NULL);
        btn = lv_list_add_button(list1, LV_SYMBOL_USB, "USB");
        lv_obj_add_event_cb(btn, event_handler, LV_EVENT_CLICKED, NULL);
        btn = lv_list_add_button(list1, LV_SYMBOL_BATTERY_FULL, "Battery");
        lv_obj_add_event_cb(btn, event_handler, LV_EVENT_CLICKED, NULL);

        lv_list_add_text(list1, "Exit");
        btn = lv_list_add_button(list1, LV_SYMBOL_OK, "Apply");
        lv_obj_add_event_cb(btn, event_handler, LV_EVENT_CLICKED, NULL);
        btn = lv_list_add_button(list1, LV_SYMBOL_CLOSE, "Close");
        lv_obj_add_event_cb(btn, event_handler, LV_EVENT_CLICKED, NULL);
    }
};

class LvExampleList2 : public LvglComponensBase {
public:
    inline static lv_obj_t *list1;
    inline static lv_obj_t *list2;

    inline static lv_obj_t *currentButton = NULL;

    static void event_handler(lv_event_t *e)
    {
        lv_event_code_t code = lv_event_get_code(e);
        lv_obj_t *obj        = lv_event_get_target(e);
        if (code == LV_EVENT_CLICKED) {
            LV_LOG_USER("Clicked: %s", lv_list_get_button_text(list1, obj));

            if (currentButton == obj) {
                currentButton = NULL;
            } else {
                currentButton = obj;
            }
            lv_obj_t *parent = lv_obj_get_parent(obj);
            uint32_t i;
            for (i = 0; i < lv_obj_get_child_count(parent); i++) {
                lv_obj_t *child = lv_obj_get_child(parent, i);
                if (child == currentButton) {
                    lv_obj_add_state(child, LV_STATE_CHECKED);
                } else {
                    lv_obj_remove_state(child, LV_STATE_CHECKED);
                }
            }
        }
    }

    static void event_handler_top(lv_event_t *e)
    {
        lv_event_code_t code = lv_event_get_code(e);
        if (code == LV_EVENT_CLICKED) {
            if (currentButton == NULL) return;
            lv_obj_move_background(currentButton);
            lv_obj_scroll_to_view(currentButton, LV_ANIM_ON);
        }
    }

    static void event_handler_up(lv_event_t *e)
    {
        lv_event_code_t code = lv_event_get_code(e);
        if ((code == LV_EVENT_CLICKED) || (code == LV_EVENT_LONG_PRESSED_REPEAT)) {
            if (currentButton == NULL) return;
            uint32_t index = lv_obj_get_index(currentButton);
            if (index <= 0) return;
            lv_obj_move_to_index(currentButton, index - 1);
            lv_obj_scroll_to_view(currentButton, LV_ANIM_ON);
        }
    }

    static void event_handler_center(lv_event_t *e)
    {
        const lv_event_code_t code = lv_event_get_code(e);
        if ((code == LV_EVENT_CLICKED) || (code == LV_EVENT_LONG_PRESSED_REPEAT)) {
            if (currentButton == NULL) return;

            lv_obj_t *parent   = lv_obj_get_parent(currentButton);
            const uint32_t pos = lv_obj_get_child_count(parent) / 2;

            lv_obj_move_to_index(currentButton, pos);

            lv_obj_scroll_to_view(currentButton, LV_ANIM_ON);
        }
    }

    static void event_handler_dn(lv_event_t *e)
    {
        const lv_event_code_t code = lv_event_get_code(e);
        if ((code == LV_EVENT_CLICKED) || (code == LV_EVENT_LONG_PRESSED_REPEAT)) {
            if (currentButton == NULL) return;
            const uint32_t index = lv_obj_get_index(currentButton);

            lv_obj_move_to_index(currentButton, index + 1);
            lv_obj_scroll_to_view(currentButton, LV_ANIM_ON);
        }
    }

    static void event_handler_bottom(lv_event_t *e)
    {
        const lv_event_code_t code = lv_event_get_code(e);
        if (code == LV_EVENT_CLICKED) {
            if (currentButton == NULL) return;
            lv_obj_move_foreground(currentButton);
            lv_obj_scroll_to_view(currentButton, LV_ANIM_ON);
        }
    }

    static void event_handler_swap(lv_event_t *e)
    {
        const lv_event_code_t code = lv_event_get_code(e);
        // lv_obj_t* obj = lv_event_get_target(e);
        if ((code == LV_EVENT_CLICKED) || (code == LV_EVENT_LONG_PRESSED_REPEAT)) {
            uint32_t cnt = lv_obj_get_child_count(list1);
            for (int i = 0; i < 100; i++)
                if (cnt > 1) {
                    lv_obj_t *obj = lv_obj_get_child(list1, lv_rand(0, cnt));
                    lv_obj_move_to_index(obj, lv_rand(0, cnt));
                    if (currentButton != NULL) {
                        lv_obj_scroll_to_view(currentButton, LV_ANIM_ON);
                    }
                }
        }
    }

    LvExampleList2() = default;
    LvExampleList2(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        /*Create a list*/
        list1 = lv_list_create(parent);
        lv_obj_set_size(list1, lv_pct(60), lv_pct(100));
        lv_obj_set_style_pad_row(list1, 5, 0);

        /*Add buttons to the list*/
        lv_obj_t *btn;
        int i;
        for (i = 0; i < 15; i++) {
            btn = lv_button_create(list1);
            lv_obj_set_width(btn, lv_pct(50));
            lv_obj_add_event_cb(btn, event_handler, LV_EVENT_CLICKED, NULL);

            lv_obj_t *lab = lv_label_create(btn);
            lv_label_set_text_fmt(lab, "Item %d", i);
        }

        /*Select the first button by default*/
        currentButton = lv_obj_get_child(list1, 0);
        lv_obj_add_state(currentButton, LV_STATE_CHECKED);

        /*Create a second list with up and down buttons*/
        list2 = lv_list_create(parent);
        lv_obj_set_size(list2, lv_pct(40), lv_pct(100));
        lv_obj_align(list2, LV_ALIGN_TOP_RIGHT, 0, 0);
        lv_obj_set_flex_flow(list2, LV_FLEX_FLOW_COLUMN);

        btn = lv_list_add_button(list2, NULL, "Top");
        lv_obj_add_event_cb(btn, event_handler_top, LV_EVENT_ALL, NULL);
        lv_group_remove_obj(btn);

        btn = lv_list_add_button(list2, LV_SYMBOL_UP, "Up");
        lv_obj_add_event_cb(btn, event_handler_up, LV_EVENT_ALL, NULL);
        lv_group_remove_obj(btn);

        btn = lv_list_add_button(list2, LV_SYMBOL_LEFT, "Center");
        lv_obj_add_event_cb(btn, event_handler_center, LV_EVENT_ALL, NULL);
        lv_group_remove_obj(btn);

        btn = lv_list_add_button(list2, LV_SYMBOL_DOWN, "Down");
        lv_obj_add_event_cb(btn, event_handler_dn, LV_EVENT_ALL, NULL);
        lv_group_remove_obj(btn);

        btn = lv_list_add_button(list2, NULL, "Bottom");
        lv_obj_add_event_cb(btn, event_handler_bottom, LV_EVENT_ALL, NULL);
        lv_group_remove_obj(btn);

        btn = lv_list_add_button(list2, LV_SYMBOL_SHUFFLE, "Shuffle");
        lv_obj_add_event_cb(btn, event_handler_swap, LV_EVENT_ALL, NULL);
        lv_group_remove_obj(btn);
    }
};

class LvExampleMenu1 : public LvglComponensBase {
public:
    LvExampleMenu1() = default;
    LvExampleMenu1(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        /*Create a menu object*/
        lv_obj_t *menu = lv_menu_create(parent);
        lv_obj_set_size(menu, lv_display_get_horizontal_resolution(NULL), lv_display_get_vertical_resolution(NULL));
        lv_obj_center(menu);

        lv_obj_t *cont;
        lv_obj_t *label;

        /*Create a sub page*/
        lv_obj_t *sub_page = lv_menu_page_create(menu, NULL);

        cont  = lv_menu_cont_create(sub_page);
        label = lv_label_create(cont);
        lv_label_set_text(label, "Hello, I am hiding here");

        /*Create a main page*/
        lv_obj_t *main_page = lv_menu_page_create(menu, NULL);

        cont  = lv_menu_cont_create(main_page);
        label = lv_label_create(cont);
        lv_label_set_text(label, "Item 1");

        cont  = lv_menu_cont_create(main_page);
        label = lv_label_create(cont);
        lv_label_set_text(label, "Item 2");

        cont  = lv_menu_cont_create(main_page);
        label = lv_label_create(cont);
        lv_label_set_text(label, "Item 3 (Click me!)");
        lv_menu_set_load_page_event(menu, cont, sub_page);

        lv_menu_set_page(menu, main_page);
    }
};

class LvExampleMenu2 : public LvglComponensBase {
public:
    static void back_event_handler(lv_event_t *e)
    {
        lv_obj_t *obj  = lv_event_get_target(e);
        lv_obj_t *menu = lv_event_get_user_data(e);

        if (lv_menu_back_button_is_root(menu, obj)) {
            lv_obj_t *mbox1 = lv_msgbox_create(NULL);
            lv_msgbox_add_title(mbox1, "Hello");
            lv_msgbox_add_text(mbox1, "Root back btn click.");
            lv_msgbox_add_close_button(mbox1);
        }
    }

    LvExampleMenu2() = default;
    LvExampleMenu2(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        lv_obj_t *menu = lv_menu_create(parent);
        lv_menu_set_mode_root_back_button(menu, LV_MENU_ROOT_BACK_BUTTON_ENABLED);
        lv_obj_add_event_cb(menu, back_event_handler, LV_EVENT_CLICKED, menu);
        lv_obj_set_size(menu, lv_display_get_horizontal_resolution(NULL), lv_display_get_vertical_resolution(NULL));
        lv_obj_center(menu);

        lv_obj_t *cont;
        lv_obj_t *label;

        /*Create a sub page*/
        lv_obj_t *sub_page = lv_menu_page_create(menu, NULL);

        cont  = lv_menu_cont_create(sub_page);
        label = lv_label_create(cont);
        lv_label_set_text(label, "Hello, I am hiding here");

        /*Create a main page*/
        lv_obj_t *main_page = lv_menu_page_create(menu, NULL);

        cont  = lv_menu_cont_create(main_page);
        label = lv_label_create(cont);
        lv_label_set_text(label, "Item 1");

        cont  = lv_menu_cont_create(main_page);
        label = lv_label_create(cont);
        lv_label_set_text(label, "Item 2");

        cont  = lv_menu_cont_create(main_page);
        label = lv_label_create(cont);
        lv_label_set_text(label, "Item 3 (Click me!)");
        lv_menu_set_load_page_event(menu, cont, sub_page);

        lv_menu_set_page(menu, main_page);
    }
};

class LvExampleMenu3 : public LvglComponensBase {
public:
    LvExampleMenu3() = default;
    LvExampleMenu3(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        /*Create a menu object*/
        lv_obj_t *menu = lv_menu_create(parent);
        lv_obj_set_size(menu, lv_display_get_horizontal_resolution(NULL), lv_display_get_vertical_resolution(NULL));
        lv_obj_center(menu);

        /*Modify the header*/
        lv_obj_t *back_btn          = lv_menu_get_main_header_back_button(menu);
        lv_obj_t *back_button_label = lv_label_create(back_btn);
        lv_label_set_text(back_button_label, "Back");

        lv_obj_t *cont;
        lv_obj_t *label;

        /*Create sub pages*/
        lv_obj_t *sub_1_page = lv_menu_page_create(menu, "Page 1");

        cont  = lv_menu_cont_create(sub_1_page);
        label = lv_label_create(cont);
        lv_label_set_text(label, "Hello, I am hiding here");

        lv_obj_t *sub_2_page = lv_menu_page_create(menu, "Page 2");

        cont  = lv_menu_cont_create(sub_2_page);
        label = lv_label_create(cont);
        lv_label_set_text(label, "Hello, I am hiding here");

        lv_obj_t *sub_3_page = lv_menu_page_create(menu, "Page 3");

        cont  = lv_menu_cont_create(sub_3_page);
        label = lv_label_create(cont);
        lv_label_set_text(label, "Hello, I am hiding here");

        /*Create a main page*/
        lv_obj_t *main_page = lv_menu_page_create(menu, NULL);

        cont  = lv_menu_cont_create(main_page);
        label = lv_label_create(cont);
        lv_label_set_text(label, "Item 1 (Click me!)");
        lv_menu_set_load_page_event(menu, cont, sub_1_page);

        cont  = lv_menu_cont_create(main_page);
        label = lv_label_create(cont);
        lv_label_set_text(label, "Item 2 (Click me!)");
        lv_menu_set_load_page_event(menu, cont, sub_2_page);

        cont  = lv_menu_cont_create(main_page);
        label = lv_label_create(cont);
        lv_label_set_text(label, "Item 3 (Click me!)");
        lv_menu_set_load_page_event(menu, cont, sub_3_page);

        lv_menu_set_page(menu, main_page);
    }
};

class LvExampleMenu4 : public LvglComponensBase {
public:
    inline static uint32_t btn_cnt = 1;
    inline static lv_obj_t *main_page;
    inline static lv_obj_t *menu;

    static void float_button_event_cb(lv_event_t *e)
    {
        LV_UNUSED(e);

        btn_cnt++;

        lv_obj_t *cont;
        lv_obj_t *label;

        lv_obj_t *sub_page = lv_menu_page_create(menu, NULL);

        cont  = lv_menu_cont_create(sub_page);
        label = lv_label_create(cont);
        lv_label_set_text_fmt(label, "Hello, I am hiding inside %" LV_PRIu32 "", btn_cnt);

        cont  = lv_menu_cont_create(main_page);
        label = lv_label_create(cont);
        lv_label_set_text_fmt(label, "Item %" LV_PRIu32 "", btn_cnt);
        lv_menu_set_load_page_event(menu, cont, sub_page);

        lv_obj_scroll_to_view_recursive(cont, LV_ANIM_ON);
    }

    LvExampleMenu4() = default;
    LvExampleMenu4(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        /*Create a menu object*/
        menu = lv_menu_create(parent);
        lv_obj_set_size(menu, lv_display_get_horizontal_resolution(NULL), lv_display_get_vertical_resolution(NULL));
        lv_obj_center(menu);

        lv_obj_t *cont;
        lv_obj_t *label;

        /*Create a sub page*/
        lv_obj_t *sub_page = lv_menu_page_create(menu, NULL);

        cont  = lv_menu_cont_create(sub_page);
        label = lv_label_create(cont);
        lv_label_set_text(label, "Hello, I am hiding inside the first item");

        /*Create a main page*/
        main_page = lv_menu_page_create(menu, NULL);

        cont  = lv_menu_cont_create(main_page);
        label = lv_label_create(cont);
        lv_label_set_text(label, "Item 1");
        lv_menu_set_load_page_event(menu, cont, sub_page);

        lv_menu_set_page(menu, main_page);

        /*Create floating btn*/
        lv_obj_t *float_btn = lv_button_create(parent);
        lv_obj_set_size(float_btn, 50, 50);
        lv_obj_add_flag(float_btn, LV_OBJ_FLAG_FLOATING);
        lv_obj_align(float_btn, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
        lv_obj_add_event_cb(float_btn, float_button_event_cb, LV_EVENT_CLICKED, menu);
        lv_obj_set_style_radius(float_btn, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_image_src(float_btn, LV_SYMBOL_PLUS, 0);
        lv_obj_set_style_text_font(float_btn, lv_theme_get_font_large(float_btn), 0);
    }
};

class LvExampleMenu5 : public LvglComponensBase {
public:
    typedef enum { LV_MENU_ITEM_BUILDER_VARIANT_1, LV_MENU_ITEM_BUILDER_VARIANT_2 } lv_menu_builder_variant_t;

    static void back_event_handler(lv_event_t *e);
    static void switch_handler(lv_event_t *e);
    lv_obj_t *root_page;
    inline static lv_obj_t *create_text(lv_obj_t *parent, const char *icon, const char *txt,
                                        lv_menu_builder_variant_t builder_variant);
    inline static lv_obj_t *create_slider(lv_obj_t *parent, const char *icon, const char *txt, int32_t min, int32_t max,
                                          int32_t val);
    inline static lv_obj_t *create_switch(lv_obj_t *parent, const char *icon, const char *txt, bool chk);

    LvExampleMenu5() = default;
    LvExampleMenu5(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        lv_obj_t *menu = lv_menu_create(parent);

        lv_color_t bg_color = lv_obj_get_style_bg_color(menu, 0);
        if (lv_color_brightness(bg_color) > 127) {
            lv_obj_set_style_bg_color(menu, lv_color_darken(lv_obj_get_style_bg_color(menu, 0), 10), 0);
        } else {
            lv_obj_set_style_bg_color(menu, lv_color_darken(lv_obj_get_style_bg_color(menu, 0), 50), 0);
        }
        lv_menu_set_mode_root_back_button(menu, LV_MENU_ROOT_BACK_BUTTON_ENABLED);
        lv_obj_add_event_cb(menu, back_event_handler, LV_EVENT_CLICKED, menu);
        lv_obj_set_size(menu, lv_display_get_horizontal_resolution(NULL), lv_display_get_vertical_resolution(NULL));
        lv_obj_center(menu);

        lv_obj_t *cont;
        lv_obj_t *section;

        /*Create sub pages*/
        lv_obj_t *sub_mechanics_page = lv_menu_page_create(menu, NULL);
        lv_obj_set_style_pad_hor(sub_mechanics_page, lv_obj_get_style_pad_left(lv_menu_get_main_header(menu), 0), 0);
        lv_menu_separator_create(sub_mechanics_page);
        section = lv_menu_section_create(sub_mechanics_page);
        create_slider(section, LV_SYMBOL_SETTINGS, "Velocity", 0, 150, 120);
        create_slider(section, LV_SYMBOL_SETTINGS, "Acceleration", 0, 150, 50);
        create_slider(section, LV_SYMBOL_SETTINGS, "Weight limit", 0, 150, 80);

        lv_obj_t *sub_sound_page = lv_menu_page_create(menu, NULL);
        lv_obj_set_style_pad_hor(sub_sound_page, lv_obj_get_style_pad_left(lv_menu_get_main_header(menu), 0), 0);
        lv_menu_separator_create(sub_sound_page);
        section = lv_menu_section_create(sub_sound_page);
        create_switch(section, LV_SYMBOL_AUDIO, "Sound", false);

        lv_obj_t *sub_display_page = lv_menu_page_create(menu, NULL);
        lv_obj_set_style_pad_hor(sub_display_page, lv_obj_get_style_pad_left(lv_menu_get_main_header(menu), 0), 0);
        lv_menu_separator_create(sub_display_page);
        section = lv_menu_section_create(sub_display_page);
        create_slider(section, LV_SYMBOL_SETTINGS, "Brightness", 0, 150, 100);

        lv_obj_t *sub_software_info_page = lv_menu_page_create(menu, NULL);
        lv_obj_set_style_pad_hor(sub_software_info_page, lv_obj_get_style_pad_left(lv_menu_get_main_header(menu), 0),
                                 0);
        section = lv_menu_section_create(sub_software_info_page);
        create_text(section, NULL, "Version 1.0", LV_MENU_ITEM_BUILDER_VARIANT_1);

        lv_obj_t *sub_legal_info_page = lv_menu_page_create(menu, NULL);
        lv_obj_set_style_pad_hor(sub_legal_info_page, lv_obj_get_style_pad_left(lv_menu_get_main_header(menu), 0), 0);
        section = lv_menu_section_create(sub_legal_info_page);
        for (uint32_t i = 0; i < 15; i++) {
            create_text(
                section, NULL,
                "This is a long long long long long long long long long text, if it is long enough it may scroll.",
                LV_MENU_ITEM_BUILDER_VARIANT_1);
        }

        lv_obj_t *sub_about_page = lv_menu_page_create(menu, NULL);
        lv_obj_set_style_pad_hor(sub_about_page, lv_obj_get_style_pad_left(lv_menu_get_main_header(menu), 0), 0);
        lv_menu_separator_create(sub_about_page);
        section = lv_menu_section_create(sub_about_page);
        cont    = create_text(section, NULL, "Software information", LV_MENU_ITEM_BUILDER_VARIANT_1);
        lv_menu_set_load_page_event(menu, cont, sub_software_info_page);
        cont = create_text(section, NULL, "Legal information", LV_MENU_ITEM_BUILDER_VARIANT_1);
        lv_menu_set_load_page_event(menu, cont, sub_legal_info_page);

        lv_obj_t *sub_menu_mode_page = lv_menu_page_create(menu, NULL);
        lv_obj_set_style_pad_hor(sub_menu_mode_page, lv_obj_get_style_pad_left(lv_menu_get_main_header(menu), 0), 0);
        lv_menu_separator_create(sub_menu_mode_page);
        section = lv_menu_section_create(sub_menu_mode_page);
        cont    = create_switch(section, LV_SYMBOL_AUDIO, "Sidebar enable", true);
        lv_obj_add_event_cb(lv_obj_get_child(cont, 2), switch_handler, LV_EVENT_VALUE_CHANGED, menu);

        /*Create a root page*/
        root_page = lv_menu_page_create(menu, "Settings");
        lv_obj_set_style_pad_hor(root_page, lv_obj_get_style_pad_left(lv_menu_get_main_header(menu), 0), 0);
        section = lv_menu_section_create(root_page);
        cont    = create_text(section, LV_SYMBOL_SETTINGS, "Mechanics", LV_MENU_ITEM_BUILDER_VARIANT_1);
        lv_menu_set_load_page_event(menu, cont, sub_mechanics_page);
        cont = create_text(section, LV_SYMBOL_AUDIO, "Sound", LV_MENU_ITEM_BUILDER_VARIANT_1);
        lv_menu_set_load_page_event(menu, cont, sub_sound_page);
        cont = create_text(section, LV_SYMBOL_SETTINGS, "Display", LV_MENU_ITEM_BUILDER_VARIANT_1);
        lv_menu_set_load_page_event(menu, cont, sub_display_page);

        create_text(root_page, NULL, "Others", LV_MENU_ITEM_BUILDER_VARIANT_1);
        section = lv_menu_section_create(root_page);
        cont    = create_text(section, NULL, "About", LV_MENU_ITEM_BUILDER_VARIANT_1);
        lv_menu_set_load_page_event(menu, cont, sub_about_page);
        cont = create_text(section, LV_SYMBOL_SETTINGS, "Menu mode", LV_MENU_ITEM_BUILDER_VARIANT_1);
        lv_menu_set_load_page_event(menu, cont, sub_menu_mode_page);

        lv_menu_set_sidebar_page(menu, root_page);

        lv_obj_send_event(lv_obj_get_child(lv_obj_get_child(lv_menu_get_cur_sidebar_page(menu), 0), 0),
                          LV_EVENT_CLICKED, NULL);
    }

    static void back_event_handler(lv_event_t *e)
    {
        lv_obj_t *obj  = lv_event_get_target(e);
        lv_obj_t *menu = lv_event_get_user_data(e);

        if (lv_menu_back_button_is_root(menu, obj)) {
            lv_obj_t *mbox1 = lv_msgbox_create(NULL);
            lv_msgbox_add_title(mbox1, "Hello");
            lv_msgbox_add_text(mbox1, "Root back btn click.");
            lv_msgbox_add_close_button(mbox1);
        }
    }

    static void switch_handler(lv_event_t *e)
    {
        lv_event_code_t code = lv_event_get_code(e);
        lv_obj_t *menu       = lv_event_get_user_data(e);
        lv_obj_t *obj        = lv_event_get_target(e);
        if (code == LV_EVENT_VALUE_CHANGED) {
            if (lv_obj_has_state(obj, LV_STATE_CHECKED)) {
                lv_menu_set_page(menu, NULL);
                lv_menu_set_sidebar_page(menu, root_page);
                lv_obj_send_event(lv_obj_get_child(lv_obj_get_child(lv_menu_get_cur_sidebar_page(menu), 0), 0),
                                  LV_EVENT_CLICKED, NULL);
            } else {
                lv_menu_set_sidebar_page(menu, NULL);
                lv_menu_clear_history(menu); /* Clear history because we will be showing the root page later */
                lv_menu_set_page(menu, root_page);
            }
        }
    }

    inline static lv_obj_t *create_text(lv_obj_t *parent, const char *icon, const char *txt,
                                        lv_menu_builder_variant_t builder_variant)
    {
        lv_obj_t *obj = lv_menu_cont_create(parent);

        lv_obj_t *img   = NULL;
        lv_obj_t *label = NULL;

        if (icon) {
            img = lv_image_create(obj);
            lv_image_set_src(img, icon);
        }

        if (txt) {
            label = lv_label_create(obj);
            lv_label_set_text(label, txt);
            lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
            lv_obj_set_flex_grow(label, 1);
        }

        if (builder_variant == LV_MENU_ITEM_BUILDER_VARIANT_2 && icon && txt) {
            lv_obj_add_flag(img, LV_OBJ_FLAG_FLEX_IN_NEW_TRACK);
            lv_obj_swap(img, label);
        }

        return obj;
    }

    inline static lv_obj_t *create_slider(lv_obj_t *parent, const char *icon, const char *txt, int32_t min, int32_t max,
                                          int32_t val)
    {
        lv_obj_t *obj = create_text(parent, icon, txt, LV_MENU_ITEM_BUILDER_VARIANT_2);

        lv_obj_t *slider = lv_slider_create(obj);
        lv_obj_set_flex_grow(slider, 1);
        lv_slider_set_range(slider, min, max);
        lv_slider_set_value(slider, val, LV_ANIM_OFF);

        if (icon == NULL) {
            lv_obj_add_flag(slider, LV_OBJ_FLAG_FLEX_IN_NEW_TRACK);
        }

        return obj;
    }

    inline static lv_obj_t *create_switch(lv_obj_t *parent, const char *icon, const char *txt, bool chk)
    {
        lv_obj_t *obj = create_text(parent, icon, txt, LV_MENU_ITEM_BUILDER_VARIANT_1);

        lv_obj_t *sw = lv_switch_create(obj);
        lv_obj_add_state(sw, chk ? LV_STATE_CHECKED : 0);

        return obj;
    }
};

class LvExampleMsgbox1 : public LvglComponensBase {
public:
    static void event_cb(lv_event_t *e)
    {
        lv_obj_t *btn   = lv_event_get_target(e);
        lv_obj_t *label = lv_obj_get_child(btn, 0);
        LV_UNUSED(label);
        LV_LOG_USER("Button %s clicked", lv_label_get_text(label));
    }

    LvExampleMsgbox1() = default;
    LvExampleMsgbox1(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        lv_obj_t *mbox1 = lv_msgbox_create(NULL);

        lv_msgbox_add_title(mbox1, "Hello");

        lv_msgbox_add_text(mbox1, "This is a message box with two buttons.");
        lv_msgbox_add_close_button(mbox1);

        lv_obj_t *btn;
        btn = lv_msgbox_add_footer_button(mbox1, "Apply");
        lv_obj_add_event_cb(btn, event_cb, LV_EVENT_CLICKED, NULL);
        btn = lv_msgbox_add_footer_button(mbox1, "Cancel");
        lv_obj_add_event_cb(btn, event_cb, LV_EVENT_CLICKED, NULL);
        return;
    }
};

class LvExampleMsgbox2 : public LvglComponensBase {
public:
    static void minimize_button_event_cb(lv_event_t *e)
    {
        lv_obj_t *mbox = (lv_obj_t *)lv_event_get_user_data(e);
        lv_obj_add_flag(mbox, LV_OBJ_FLAG_HIDDEN);
    }

    LvExampleMsgbox2() = default;
    LvExampleMsgbox2(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        lv_obj_t *setting = lv_msgbox_create(parent);
        lv_obj_set_style_clip_corner(setting, true, 0);

        /* setting fixed size */
        lv_obj_set_size(setting, 300, 200);

        /* setting's titlebar/header */
        lv_msgbox_add_title(setting, "Setting");
        lv_obj_t *minimize_button = lv_msgbox_add_header_button(setting, LV_SYMBOL_MINUS);
        lv_obj_add_event_cb(minimize_button, minimize_button_event_cb, LV_EVENT_CLICKED, setting);
        lv_msgbox_add_close_button(setting);

        /* setting's content*/
        lv_obj_t *content = lv_msgbox_get_content(setting);
        lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_right(content, -1, LV_PART_SCROLLBAR);

        lv_obj_t *cont_brightness = lv_obj_create(content);
        lv_obj_set_size(cont_brightness, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(cont_brightness, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(cont_brightness, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);

        lv_obj_t *lb_brightness = lv_label_create(cont_brightness);
        lv_label_set_text(lb_brightness, "Brightness : ");
        lv_obj_t *slider_brightness = lv_slider_create(cont_brightness);
        lv_obj_set_width(slider_brightness, lv_pct(100));
        lv_slider_set_value(slider_brightness, 50, LV_ANIM_OFF);

        lv_obj_t *cont_speed = lv_obj_create(content);
        lv_obj_set_size(cont_speed, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(cont_speed, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(cont_speed, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);

        lv_obj_t *lb_speed = lv_label_create(cont_speed);
        lv_label_set_text(lb_speed, "Speed : ");
        lv_obj_t *slider_speed = lv_slider_create(cont_speed);
        lv_obj_set_width(slider_speed, lv_pct(100));
        lv_slider_set_value(slider_speed, 80, LV_ANIM_OFF);

        /* footer */
        lv_obj_t *apply_button = lv_msgbox_add_footer_button(setting, "Apply");
        lv_obj_set_flex_grow(apply_button, 1);

        lv_obj_t *cancel_button = lv_msgbox_add_footer_button(setting, "Cancel");
        lv_obj_set_flex_grow(cancel_button, 1);

        lv_obj_t *footer = lv_msgbox_get_footer(setting);
        lv_obj_set_style_bg_color(footer, lv_palette_main(LV_PALETTE_INDIGO), 0);
        lv_obj_set_style_bg_opa(footer, LV_OPA_100, 0);
    }
};

#endif

class LvExampleRoller1 : public LvglComponensBase {
public:
    static void event_handler(lv_event_t *e)
    {
        lv_event_code_t code = lv_event_get_code(e);
        lv_obj_t *obj        = lv_event_get_target_obj(e);
        if (code == LV_EVENT_VALUE_CHANGED) {
            char buf[32];
            lv_roller_get_selected_str(obj, buf, sizeof(buf));
            LV_LOG_USER("Selected month: %s\n", buf);
        }
    }

    /**
     * An infinite roller with the name of the months
     */
    LvExampleRoller1() = default;
    LvExampleRoller1(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        lv_obj_t *roller1 = lv_roller_create(parent);
        lv_roller_set_options(roller1,
                              "January\n"
                              "February\n"
                              "March\n"
                              "April\n"
                              "May\n"
                              "June\n"
                              "July\n"
                              "August\n"
                              "September\n"
                              "October\n"
                              "November\n"
                              "December",
                              LV_ROLLER_MODE_INFINITE);

        lv_roller_set_visible_row_count(roller1, 4);
        lv_obj_center(roller1);
        lv_obj_add_event_cb(roller1, event_handler, LV_EVENT_ALL, NULL);
    }
};

#if defined(LVGL_COMPONENTS_ENABLE_EXAMPLES)
class LvExampleRoller2 : public LvglComponensBase {
public:
    static void event_handler(lv_event_t *e)
    {
        lv_event_code_t code = lv_event_get_code(e);
        lv_obj_t *obj        = lv_event_get_target(e);
        if (code == LV_EVENT_VALUE_CHANGED) {
            char buf[32];
            lv_roller_get_selected_str(obj, buf, sizeof(buf));
            LV_LOG_USER("Selected value: %s", buf);
        }
    }

    /**
     * Roller with various alignments and larger text in the selected area
     */
    LvExampleRoller2() = default;
    LvExampleRoller2(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        /*A style to make the selected option larger*/
        static lv_style_t style_sel;
        lv_style_init(&style_sel);
        lv_style_set_text_font(&style_sel, &lv_font_montserrat_22);
        lv_style_set_bg_color(&style_sel, lv_color_hex3(0xf88));
        lv_style_set_border_width(&style_sel, 2);
        lv_style_set_border_color(&style_sel, lv_color_hex3(0xf00));

        const char *opts = "1\n2\n3\n4\n5\n6\n7\n8\n9\n10";
        lv_obj_t *roller;

        /*A roller on the left with left aligned text, and custom width*/
        roller = lv_roller_create(parent);
        lv_roller_set_options(roller, opts, LV_ROLLER_MODE_NORMAL);
        lv_roller_set_visible_row_count(roller, 2);
        lv_obj_set_width(roller, 100);
        lv_obj_add_style(roller, &style_sel, LV_PART_SELECTED);
        lv_obj_set_style_text_align(roller, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_set_style_bg_color(roller, lv_color_hex3(0x0f0), 0);
        lv_obj_set_style_bg_grad_color(roller, lv_color_hex3(0xafa), 0);
        lv_obj_set_style_bg_grad_dir(roller, LV_GRAD_DIR_VER, 0);
        lv_obj_align(roller, LV_ALIGN_LEFT_MID, 10, 0);
        lv_obj_add_event_cb(roller, event_handler, LV_EVENT_ALL, NULL);
        lv_roller_set_selected(roller, 2, LV_ANIM_OFF);

        /*A roller on the middle with center aligned text, and auto (default) width*/
        roller = lv_roller_create(parent);
        lv_roller_set_options(roller, opts, LV_ROLLER_MODE_NORMAL);
        lv_roller_set_visible_row_count(roller, 3);
        lv_obj_add_style(roller, &style_sel, LV_PART_SELECTED);
        lv_obj_align(roller, LV_ALIGN_CENTER, 0, 0);
        lv_obj_add_event_cb(roller, event_handler, LV_EVENT_ALL, NULL);
        lv_roller_set_selected(roller, 5, LV_ANIM_OFF);

        /*A roller on the right with right aligned text, and custom width*/
        roller = lv_roller_create(parent);
        lv_roller_set_options(roller, opts, LV_ROLLER_MODE_NORMAL);
        lv_roller_set_visible_row_count(roller, 4);
        lv_obj_set_width(roller, 80);
        lv_obj_add_style(roller, &style_sel, LV_PART_SELECTED);
        lv_obj_set_style_text_align(roller, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_align(roller, LV_ALIGN_RIGHT_MID, -10, 0);
        lv_obj_add_event_cb(roller, event_handler, LV_EVENT_ALL, NULL);
        lv_roller_set_selected(roller, 8, LV_ANIM_OFF);
    }
};
#endif

class LvExampleRoller3 : public LvglComponensBase {
public:
    static void generate_mask(lv_draw_buf_t *mask)
    {
        /*Create a "8 bit alpha" canvas and clear it*/
        lv_obj_t *canvas = lv_canvas_create(lv_screen_active());
        lv_canvas_set_draw_buf(canvas, mask);
        lv_canvas_fill_bg(canvas, lv_color_white(), LV_OPA_TRANSP);

        lv_layer_t layer;
        lv_canvas_init_layer(canvas, &layer);

        /*Draw a label to the canvas. The result "image" will be used as mask*/
        lv_draw_rect_dsc_t rect_dsc;
        lv_draw_rect_dsc_init(&rect_dsc);
        rect_dsc.bg_grad.dir            = LV_GRAD_DIR_VER;
        rect_dsc.bg_grad.stops[0].color = lv_color_black();
        rect_dsc.bg_grad.stops[1].color = lv_color_white();
        rect_dsc.bg_grad.stops[0].opa   = LV_OPA_COVER;
        rect_dsc.bg_grad.stops[1].opa   = LV_OPA_COVER;
        lv_area_t a                     = {0, 0, mask->header.w - 1, mask->header.h / 2 - 10};
        lv_draw_rect(&layer, &rect_dsc, &a);

        a.y1                            = mask->header.h / 2 + 10;
        a.y2                            = mask->header.h - 1;
        rect_dsc.bg_grad.stops[0].color = lv_color_white();
        rect_dsc.bg_grad.stops[1].color = lv_color_black();
        lv_draw_rect(&layer, &rect_dsc, &a);

        lv_canvas_finish_layer(canvas, &layer);

        /*Comment it to make the mask visible*/
        lv_obj_delete(canvas);
    }

    /**
     * Add a fade mask to roller.
     */
    LvExampleRoller3() = default;
    LvExampleRoller3(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        lv_obj_set_style_bg_color(parent, lv_palette_main(LV_PALETTE_BLUE_GREY), 0);

        static lv_style_t style;
        lv_style_init(&style);
        lv_style_set_bg_color(&style, lv_color_black());
        lv_style_set_text_color(&style, lv_color_white());
        lv_style_set_border_width(&style, 0);
        lv_style_set_radius(&style, 0);

        lv_obj_t *roller1 = lv_roller_create(parent);
        lv_obj_add_style(roller1, &style, 0);
        lv_obj_set_style_bg_opa(roller1, LV_OPA_50, LV_PART_SELECTED);

        lv_roller_set_options(roller1,
                              "January\n"
                              "February\n"
                              "March\n"
                              "April\n"
                              "May\n"
                              "June\n"
                              "July\n"
                              "August\n"
                              "September\n"
                              "October\n"
                              "November\n"
                              "December",
                              LV_ROLLER_MODE_NORMAL);

        lv_obj_center(roller1);
        lv_roller_set_visible_row_count(roller1, 4);

        /* Create the mask to make the top and bottom part of roller faded.
         * The width and height are empirical values for simplicity*/
        LV_DRAW_BUF_DEFINE_STATIC(mask, 130, 150, LV_COLOR_FORMAT_L8);
        LV_DRAW_BUF_INIT_STATIC(mask);

        generate_mask(&mask);
        lv_obj_set_style_bitmap_mask_src(roller1, &mask, 0);
    }
};

#if defined(LVGL_COMPONENTS_ENABLE_EXAMPLES)
class LvExampleScale1 : public LvglComponensBase {
public:
    /**
     * A simple horizontal scale
     */
    LvExampleScale1() = default;
    LvExampleScale1(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        lv_obj_t *scale = lv_scale_create(parent);
        lv_obj_set_size(scale, lv_pct(80), 100);
        lv_scale_set_mode(scale, LV_SCALE_MODE_HORIZONTAL_BOTTOM);
        lv_obj_center(scale);

        lv_scale_set_label_show(scale, true);

        lv_scale_set_total_tick_count(scale, 31);
        lv_scale_set_major_tick_every(scale, 5);

        lv_obj_set_style_length(scale, 5, LV_PART_ITEMS);
        lv_obj_set_style_length(scale, 10, LV_PART_INDICATOR);
        lv_scale_set_range(scale, 10, 40);
    }
};

class LvExampleScale2 : public LvglComponensBase {
public:
    /**
     * An vertical scale with section and custom styling
     */
    LvExampleScale2() = default;
    LvExampleScale2(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        lv_obj_t *scale = lv_scale_create(parent);
        lv_obj_set_size(scale, 60, 200);
        lv_scale_set_label_show(scale, true);
        lv_scale_set_mode(scale, LV_SCALE_MODE_VERTICAL_RIGHT);
        lv_obj_center(scale);

        lv_scale_set_total_tick_count(scale, 21);
        lv_scale_set_major_tick_every(scale, 5);

        lv_obj_set_style_length(scale, 10, LV_PART_INDICATOR);
        lv_obj_set_style_length(scale, 5, LV_PART_ITEMS);
        lv_scale_set_range(scale, 0, 100);

        static const char *custom_labels[] = {"0 °C", "25 °C", "50 °C", "75 °C", "100 °C", NULL};
        lv_scale_set_text_src(scale, custom_labels);

        static lv_style_t indicator_style;
        lv_style_init(&indicator_style);

        /* Label style properties */
        lv_style_set_text_font(&indicator_style, LV_FONT_DEFAULT);
        lv_style_set_text_color(&indicator_style, lv_palette_darken(LV_PALETTE_BLUE, 3));

        /* Major tick properties */
        lv_style_set_line_color(&indicator_style, lv_palette_darken(LV_PALETTE_BLUE, 3));
        lv_style_set_width(&indicator_style, 10U);     /*Tick length*/
        lv_style_set_line_width(&indicator_style, 2U); /*Tick width*/
        lv_obj_add_style(scale, &indicator_style, LV_PART_INDICATOR);

        static lv_style_t minor_ticks_style;
        lv_style_init(&minor_ticks_style);
        lv_style_set_line_color(&minor_ticks_style, lv_palette_lighten(LV_PALETTE_BLUE, 2));
        lv_style_set_width(&minor_ticks_style, 5U);      /*Tick length*/
        lv_style_set_line_width(&minor_ticks_style, 2U); /*Tick width*/
        lv_obj_add_style(scale, &minor_ticks_style, LV_PART_ITEMS);

        static lv_style_t main_line_style;
        lv_style_init(&main_line_style);
        /* Main line properties */
        lv_style_set_line_color(&main_line_style, lv_palette_darken(LV_PALETTE_BLUE, 3));
        lv_style_set_line_width(&main_line_style, 2U);  // Tick width
        lv_obj_add_style(scale, &main_line_style, LV_PART_MAIN);

        /* Add a section */
        static lv_style_t section_minor_tick_style;
        static lv_style_t section_label_style;
        static lv_style_t section_main_line_style;

        lv_style_init(section_label_style);
        lv_style_init(section_minor_tick_style);
        lv_style_init(section_main_line_style);

        /* Label style properties */
        lv_style_set_text_font(section_label_style, LV_FONT_DEFAULT);
        lv_style_set_text_color(section_label_style, lv_palette_darken(LV_PALETTE_RED, 3));

        lv_style_set_line_color(section_label_style, lv_palette_darken(LV_PALETTE_RED, 3));
        lv_style_set_line_width(section_label_style, 5U); /*Tick width*/

        lv_style_set_line_color(section_minor_tick_style, lv_palette_lighten(LV_PALETTE_RED, 2));
        lv_style_set_line_width(section_minor_tick_style, 4U); /*Tick width*/

        /* Main line properties */
        lv_style_set_line_color(section_main_line_style, lv_palette_darken(LV_PALETTE_RED, 3));
        lv_style_set_line_width(section_main_line_style, 4U); /*Tick width*/

        /* Configure section styles */
        lv_scale_section_t *section = lv_scale_add_section(scale);
        lv_scale_section_set_range(section, 75, 100);
        lv_scale_section_set_style(section, LV_PART_INDICATOR, section_label_style);
        lv_scale_section_set_style(section, LV_PART_ITEMS, section_minor_tick_style);
        lv_scale_section_set_style(section, LV_PART_MAIN, section_main_line_style);

        lv_obj_set_style_bg_color(scale, lv_palette_main(LV_PALETTE_BLUE_GREY), 0);
        lv_obj_set_style_bg_opa(scale, LV_OPA_50, 0);
        lv_obj_set_style_pad_left(scale, 8, 0);
        lv_obj_set_style_radius(scale, 8, 0);
        lv_obj_set_style_pad_ver(scale, 20, 0);
    }
};

class LvExampleScale3 : public LvglComponensBase {
public:
    LV_IMAGE_DECLARE(img_hand);

    lv_obj_t *needle_line;
    lv_obj_t *needle_img;

    static void set_needle_line_value(void *obj, int32_t v)
    {
        lv_scale_set_line_needle_value(obj, needle_line, 60, v);
    }

    static void set_needle_img_value(void *obj, int32_t v)
    {
        lv_scale_set_image_needle_value(obj, needle_img, v);
    }

    /**
     * A simple round scale
     */
    LvExampleScale3() = default;
    LvExampleScale3(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        lv_obj_t *scale_line = lv_scale_create(parent);

        lv_obj_set_size(scale_line, 150, 150);
        lv_scale_set_mode(scale_line, LV_SCALE_MODE_ROUND_INNER);
        lv_obj_set_style_bg_opa(scale_line, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(scale_line, lv_palette_lighten(LV_PALETTE_RED, 5), 0);
        lv_obj_set_style_radius(scale_line, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_clip_corner(scale_line, true, 0);
        lv_obj_align(scale_line, LV_ALIGN_LEFT_MID, LV_PCT(2), 0);

        lv_scale_set_label_show(scale_line, true);

        lv_scale_set_total_tick_count(scale_line, 31);
        lv_scale_set_major_tick_every(scale_line, 5);

        lv_obj_set_style_length(scale_line, 5, LV_PART_ITEMS);
        lv_obj_set_style_length(scale_line, 10, LV_PART_INDICATOR);
        lv_scale_set_range(scale_line, 10, 40);

        lv_scale_set_angle_range(scale_line, 270);
        lv_scale_set_rotation(scale_line, 135);

        needle_line = lv_line_create(scale_line);

        lv_obj_set_style_line_width(needle_line, 6, LV_PART_MAIN);
        lv_obj_set_style_line_rounded(needle_line, true, LV_PART_MAIN);

        lv_anim_t anim_scale_line;
        lv_anim_init(&anim_scale_line);
        lv_anim_set_var(&anim_scale_line, scale_line);
        lv_anim_set_exec_cb(&anim_scale_line, set_needle_line_value);
        lv_anim_set_duration(&anim_scale_line, 1000);
        lv_anim_set_repeat_count(&anim_scale_line, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_playback_duration(&anim_scale_line, 1000);
        lv_anim_set_values(&anim_scale_line, 10, 40);
        lv_anim_start(&anim_scale_line);

        lv_obj_t *scale_img = lv_scale_create(parent);

        lv_obj_set_size(scale_img, 150, 150);
        lv_scale_set_mode(scale_img, LV_SCALE_MODE_ROUND_INNER);
        lv_obj_set_style_bg_opa(scale_img, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(scale_img, lv_palette_lighten(LV_PALETTE_RED, 5), 0);
        lv_obj_set_style_radius(scale_img, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_clip_corner(scale_img, true, 0);
        lv_obj_align(scale_img, LV_ALIGN_RIGHT_MID, LV_PCT(-2), 0);

        lv_scale_set_label_show(scale_img, true);

        lv_scale_set_total_tick_count(scale_img, 31);
        lv_scale_set_major_tick_every(scale_img, 5);

        lv_obj_set_style_length(scale_img, 5, LV_PART_ITEMS);
        lv_obj_set_style_length(scale_img, 10, LV_PART_INDICATOR);
        lv_scale_set_range(scale_img, 10, 40);

        lv_scale_set_angle_range(scale_img, 270);
        lv_scale_set_rotation(scale_img, 135);

        /* image must point to the right. E.g. -O------>*/
        needle_img = lv_image_create(scale_img);
        lv_image_set_src(needle_img, &img_hand);
        lv_obj_align(needle_img, LV_ALIGN_CENTER, 47, -2);
        lv_image_set_pivot(needle_img, 3, 4);

        lv_anim_t anim_scale_img;
        lv_anim_init(&anim_scale_img);
        lv_anim_set_var(&anim_scale_img, scale_img);
        lv_anim_set_exec_cb(&anim_scale_img, set_needle_img_value);
        lv_anim_set_duration(&anim_scale_img, 1000);
        lv_anim_set_repeat_count(&anim_scale_img, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_playback_duration(&anim_scale_img, 1000);
        lv_anim_set_values(&anim_scale_img, 10, 40);
        lv_anim_start(&anim_scale_img);
    }
};

class LvExampleScale4 : public LvglComponensBase {
public:
    /**
     * A round scale with section and custom styling
     */
    LvExampleScale4() = default;
    LvExampleScale4(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        lv_obj_t *scale = lv_scale_create(parent);
        lv_obj_set_size(scale, 150, 150);
        lv_scale_set_label_show(scale, true);
        lv_scale_set_mode(scale, LV_SCALE_MODE_ROUND_OUTER);
        lv_obj_center(scale);

        lv_scale_set_total_tick_count(scale, 21);
        lv_scale_set_major_tick_every(scale, 5);

        lv_obj_set_style_length(scale, 5, LV_PART_ITEMS);
        lv_obj_set_style_length(scale, 10, LV_PART_INDICATOR);
        lv_scale_set_range(scale, 0, 100);

        static const char *custom_labels[] = {"0 °C", "25 °C", "50 °C", "75 °C", "100 °C", NULL};
        lv_scale_set_text_src(scale, custom_labels);

        static lv_style_t indicator_style;
        lv_style_init(&indicator_style);

        /* Label style properties */
        lv_style_set_text_font(&indicator_style, LV_FONT_DEFAULT);
        lv_style_set_text_color(&indicator_style, lv_palette_darken(LV_PALETTE_BLUE, 3));

        /* Major tick properties */
        lv_style_set_line_color(&indicator_style, lv_palette_darken(LV_PALETTE_BLUE, 3));
        lv_style_set_width(&indicator_style, 10U);     /*Tick length*/
        lv_style_set_line_width(&indicator_style, 2U); /*Tick width*/
        lv_obj_add_style(scale, &indicator_style, LV_PART_INDICATOR);

        static lv_style_t minor_ticks_style;
        lv_style_init(&minor_ticks_style);
        lv_style_set_line_color(&minor_ticks_style, lv_palette_lighten(LV_PALETTE_BLUE, 2));
        lv_style_set_width(&minor_ticks_style, 5U);      /*Tick length*/
        lv_style_set_line_width(&minor_ticks_style, 2U); /*Tick width*/
        lv_obj_add_style(scale, &minor_ticks_style, LV_PART_ITEMS);

        static lv_style_t main_line_style;
        lv_style_init(&main_line_style);
        /* Main line properties */
        lv_style_set_arc_color(&main_line_style, lv_palette_darken(LV_PALETTE_BLUE, 3));
        lv_style_set_arc_width(&main_line_style, 2U); /*Tick width*/
        lv_obj_add_style(scale, &main_line_style, LV_PART_MAIN);

        /* Add a section */
        static lv_style_t section_minor_tick_style;
        static lv_style_t section_label_style;
        static lv_style_t section_main_line_style;

        lv_style_init(section_label_style);
        lv_style_init(section_minor_tick_style);
        lv_style_init(section_main_line_style);

        /* Label style properties */
        lv_style_set_text_font(section_label_style, LV_FONT_DEFAULT);
        lv_style_set_text_color(section_label_style, lv_palette_darken(LV_PALETTE_RED, 3));

        lv_style_set_line_color(section_label_style, lv_palette_darken(LV_PALETTE_RED, 3));
        lv_style_set_line_width(section_label_style, 5U); /*Tick width*/

        lv_style_set_line_color(section_minor_tick_style, lv_palette_lighten(LV_PALETTE_RED, 2));
        lv_style_set_line_width(section_minor_tick_style, 4U); /*Tick width*/

        /* Main line properties */
        lv_style_set_arc_color(section_main_line_style, lv_palette_darken(LV_PALETTE_RED, 3));
        lv_style_set_arc_width(section_main_line_style, 4U); /*Tick width*/

        /* Configure section styles */
        lv_scale_section_t *section = lv_scale_add_section(scale);
        lv_scale_section_set_range(section, 75, 100);
        lv_scale_section_set_style(section, LV_PART_INDICATOR, section_label_style);
        lv_scale_section_set_style(section, LV_PART_ITEMS, section_minor_tick_style);
        lv_scale_section_set_style(section, LV_PART_MAIN, section_main_line_style);
    }
};

class LvExampleScale5 : public LvglComponensBase {
public:
    /**
     * An scale with section and custom styling
     */
    LvExampleScale5() = default;
    LvExampleScale5(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        lv_obj_t *scale = lv_scale_create(parent);
        lv_obj_set_size(scale, lv_display_get_horizontal_resolution(NULL) / 2,
                        lv_display_get_vertical_resolution(NULL) / 2);
        lv_scale_set_label_show(scale, true);

        lv_scale_set_total_tick_count(scale, 10);
        lv_scale_set_major_tick_every(scale, 5);

        lv_obj_set_style_length(scale, 5, LV_PART_ITEMS);
        lv_obj_set_style_length(scale, 10, LV_PART_INDICATOR);
        lv_scale_set_range(scale, 25, 35);

        static const char *custom_labels[3] = {"One", "Two", NULL};
        lv_scale_set_text_src(scale, custom_labels);

        static lv_style_t indicator_style;
        lv_style_init(&indicator_style);
        /* Label style properties */
        lv_style_set_text_font(&indicator_style, LV_FONT_DEFAULT);
        lv_style_set_text_color(&indicator_style, lv_color_hex(0xff00ff));
        /* Major tick properties */
        lv_style_set_line_color(&indicator_style, lv_color_hex(0x00ff00));
        lv_style_set_width(&indicator_style, 10U);      // Tick length
        lv_style_set_line_width(&indicator_style, 2U);  // Tick width
        lv_obj_add_style(scale, &indicator_style, LV_PART_INDICATOR);

        static lv_style_t minor_ticks_style;
        lv_style_init(&minor_ticks_style);
        lv_style_set_line_color(&minor_ticks_style, lv_color_hex(0xff0000));
        lv_style_set_width(&minor_ticks_style, 5U);       // Tick length
        lv_style_set_line_width(&minor_ticks_style, 2U);  // Tick width
        lv_obj_add_style(scale, &minor_ticks_style, LV_PART_ITEMS);

        static lv_style_t main_line_style;
        lv_style_init(&main_line_style);
        /* Main line properties */
        lv_style_set_line_color(&main_line_style, lv_color_hex(0x0000ff));
        lv_style_set_line_width(&main_line_style, 2U);  // Tick width
        lv_obj_add_style(scale, &main_line_style, LV_PART_MAIN);

        lv_obj_center(scale);

        /* Add a section */
        static lv_style_t section_minor_tick_style;
        static lv_style_t section_label_style;

        lv_style_init(section_label_style);
        lv_style_init(section_minor_tick_style);

        /* Label style properties */
        lv_style_set_text_font(section_label_style, LV_FONT_DEFAULT);
        lv_style_set_text_color(section_label_style, lv_color_hex(0xff0000));
        lv_style_set_text_letter_space(section_label_style, 10);
        lv_style_set_text_opa(section_label_style, LV_OPA_50);

        lv_style_set_line_color(section_label_style, lv_color_hex(0xff0000));
        // lv_style_set_width(section_label_style, 20U); // Tick length
        lv_style_set_line_width(section_label_style, 5U);  // Tick width

        lv_style_set_line_color(section_minor_tick_style, lv_color_hex(0x0000ff));
        // lv_style_set_width(section_label_style, 20U); // Tick length
        lv_style_set_line_width(section_minor_tick_style, 4U);  // Tick width

        /* Configure section styles */
        lv_scale_section_t *section = lv_scale_add_section(scale);
        lv_scale_section_set_range(section, 25, 30);
        lv_scale_section_set_style(section, LV_PART_INDICATOR, section_label_style);
        lv_scale_section_set_style(section, LV_PART_ITEMS, section_minor_tick_style);
    }
};

class LvExampleScale6 : public LvglComponensBase {
public:
#if LV_USE_FLOAT
#define my_PRIprecise "f"
#else
#define my_PRIprecise LV_PRId32
#endif

    inline static lv_obj_t *scale;
    inline static lv_obj_t *minute_hand;
    inline static lv_obj_t *hour_hand;
    inline static lv_point_precise_t minute_hand_points[2];
    inline static int32_t hour;
    inline static int32_t minute;

    static void timer_cb(lv_timer_t *timer)
    {
        LV_UNUSED(timer);

        minute++;
        if (minute > 59) {
            minute = 0;
            hour++;
            if (hour > 11) {
                hour = 0;
            }
        }

        /**
         * the scale will store the needle line points in the existing
         * point array if one was set with `lv_line_set_points_mutable`.
         * Otherwise, it will allocate the needle line points.
         */

        /* the scale will store the minute hand line points in `minute_hand_points` */
        lv_scale_set_line_needle_value(scale, minute_hand, 60, minute);
        /* log the points that were stored in the array */
        LV_LOG_USER(
            "minute hand points - "
            "0: (%" my_PRIprecise ", %" my_PRIprecise
            "), "
            "1: (%" my_PRIprecise ", %" my_PRIprecise ")",
            minute_hand_points[0].x, minute_hand_points[0].y, minute_hand_points[1].x, minute_hand_points[1].y);

        /* the scale will allocate the hour hand line points */
        lv_scale_set_line_needle_value(scale, hour_hand, 40, hour * 5 + (minute / 12));
    }

    /**
     * A round scale with multiple needles, resembling a clock
     */
    LvExampleScale6() = default;
    LvExampleScale6(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        scale = lv_scale_create(parent);

        lv_obj_set_size(scale, 150, 150);
        lv_scale_set_mode(scale, LV_SCALE_MODE_ROUND_INNER);
        lv_obj_set_style_bg_opa(scale, LV_OPA_60, 0);
        lv_obj_set_style_bg_color(scale, lv_color_black(), 0);
        lv_obj_set_style_radius(scale, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_clip_corner(scale, true, 0);
        lv_obj_center(scale);

        lv_scale_set_label_show(scale, true);

        lv_scale_set_total_tick_count(scale, 61);
        lv_scale_set_major_tick_every(scale, 5);

        static const char *hour_ticks[] = {"12", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", NULL};
        lv_scale_set_text_src(scale, hour_ticks);

        static lv_style_t indicator_style;
        lv_style_init(&indicator_style);

        /* Label style properties */
        lv_style_set_text_font(&indicator_style, LV_FONT_DEFAULT);
        lv_style_set_text_color(&indicator_style, lv_palette_main(LV_PALETTE_YELLOW));

        /* Major tick properties */
        lv_style_set_line_color(&indicator_style, lv_palette_main(LV_PALETTE_YELLOW));
        lv_style_set_length(&indicator_style, 8);     /* tick length */
        lv_style_set_line_width(&indicator_style, 2); /* tick width */
        lv_obj_add_style(scale, &indicator_style, LV_PART_INDICATOR);

        /* Minor tick properties */
        static lv_style_t minor_ticks_style;
        lv_style_init(&minor_ticks_style);
        lv_style_set_line_color(&minor_ticks_style, lv_palette_main(LV_PALETTE_YELLOW));
        lv_style_set_length(&minor_ticks_style, 6);     /* tick length */
        lv_style_set_line_width(&minor_ticks_style, 2); /* tick width */
        lv_obj_add_style(scale, &minor_ticks_style, LV_PART_ITEMS);

        /* Main line properties */
        static lv_style_t main_line_style;
        lv_style_init(&main_line_style);
        lv_style_set_arc_color(&main_line_style, lv_color_black());
        lv_style_set_arc_width(&main_line_style, 5);
        lv_obj_add_style(scale, &main_line_style, LV_PART_MAIN);

        lv_scale_set_range(scale, 0, 60);

        lv_scale_set_angle_range(scale, 360);
        lv_scale_set_rotation(scale, 270);

        minute_hand = lv_line_create(scale);
        lv_line_set_points_mutable(minute_hand, minute_hand_points, 2);

        lv_obj_set_style_line_width(minute_hand, 3, 0);
        lv_obj_set_style_line_rounded(minute_hand, true, 0);
        lv_obj_set_style_line_color(minute_hand, lv_color_white(), 0);

        hour_hand = lv_line_create(scale);

        lv_obj_set_style_line_width(hour_hand, 5, 0);
        lv_obj_set_style_line_rounded(hour_hand, true, 0);
        lv_obj_set_style_line_color(hour_hand, lv_palette_main(LV_PALETTE_RED), 0);

        hour              = 11;
        minute            = 5;
        lv_timer_t *timer = lv_timer_create(timer_cb, 250, NULL);
        lv_timer_ready(timer);
    }
};

class LvExampleScale7 : public LvglComponensBase {
public:
    static void draw_event_cb(lv_event_t *e)
    {
        lv_obj_t *obj                       = lv_event_get_target(e);
        lv_draw_task_t *draw_task           = lv_event_get_draw_task(e);
        lv_draw_dsc_base_t *base_dsc        = lv_draw_task_get_draw_dsc(draw_task);
        lv_draw_label_dsc_t *label_draw_dsc = lv_draw_task_get_label_dsc(draw_task);
        if (base_dsc->part == LV_PART_INDICATOR) {
            if (label_draw_dsc) {
                const lv_color_t color_idx[7] = {
                    lv_palette_main(LV_PALETTE_RED),    lv_palette_main(LV_PALETTE_ORANGE),
                    lv_palette_main(LV_PALETTE_YELLOW), lv_palette_main(LV_PALETTE_GREEN),
                    lv_palette_main(LV_PALETTE_CYAN),   lv_palette_main(LV_PALETTE_BLUE),
                    lv_palette_main(LV_PALETTE_PURPLE),
                };
                uint8_t major_tick    = lv_scale_get_major_tick_every(obj);
                label_draw_dsc->color = color_idx[base_dsc->id1 / major_tick];

                /*Free the previously allocated text if needed*/
                if (label_draw_dsc->text_local) lv_free((void *)label_draw_dsc->text);

                /*Malloc the text and set text_local as 1 to make LVGL automatically free the text.
                 * (Local texts are malloc'd internally by LVGL. Mimic this behavior here too)*/
                char tmp_buffer[20] = {0}; /* Big enough buffer */
                lv_snprintf(tmp_buffer, sizeof(tmp_buffer), "%.1f", base_dsc->id2 * 1.0f);
                label_draw_dsc->text       = lv_strdup(tmp_buffer);
                label_draw_dsc->text_local = 1;

                lv_point_t size;
                lv_text_get_size(&size, label_draw_dsc->text, label_draw_dsc->font, 0, 0, 1000, LV_TEXT_FLAG_NONE);
                int32_t new_w = size.x;
                int32_t old_w = lv_area_get_width(&draw_task->area);

                /* Distribute the new size equally on both sides */
                draw_task->area.x1 -= (new_w - old_w) / 2;
                draw_task->area.x2 += ((new_w - old_w) + 1) / 2; /* +1 for rounding */
            }
        }
    }

    /**
     * Customizing scale major tick label color with `LV_EVENT_DRAW_TASK_ADDED` event
     */
    LvExampleScale7() = default;
    LvExampleScale7(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        lv_obj_t *scale = lv_scale_create(parent);
        lv_obj_set_size(scale, lv_pct(80), 100);
        lv_scale_set_mode(scale, LV_SCALE_MODE_HORIZONTAL_BOTTOM);
        lv_obj_center(scale);

        lv_scale_set_label_show(scale, true);

        lv_scale_set_total_tick_count(scale, 31);
        lv_scale_set_major_tick_every(scale, 5);

        lv_obj_set_style_length(scale, 5, LV_PART_ITEMS);
        lv_obj_set_style_length(scale, 10, LV_PART_INDICATOR);
        lv_scale_set_range(scale, 10, 40);

        lv_obj_add_event_cb(scale, draw_event_cb, LV_EVENT_DRAW_TASK_ADDED, NULL);
        lv_obj_add_flag(scale, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);
    }
};

class LvExampleScale8 : public LvglComponensBase {
public:
    /**
     * A simple round scale with label/tick translation
     */
    LvExampleScale8() = default;
    LvExampleScale8(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        lv_obj_t *scale_line = lv_scale_create(parent);
        lv_obj_set_size(scale_line, 150, 150);
        lv_scale_set_mode(scale_line, LV_SCALE_MODE_ROUND_INNER);
        lv_obj_set_style_bg_opa(scale_line, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(scale_line, lv_palette_lighten(LV_PALETTE_RED, 5), 0);
        lv_obj_set_style_radius(scale_line, LV_RADIUS_CIRCLE, 0);
        lv_obj_align(scale_line, LV_ALIGN_LEFT_MID, LV_PCT(2), 0);

        /*Set the texts' and major ticks' style (make the texts rotated)*/
        lv_obj_set_style_transform_rotation(
            scale_line, LV_SCALE_LABEL_ROTATE_MATCH_TICKS | LV_SCALE_LABEL_ROTATE_KEEP_UPRIGHT, LV_PART_INDICATOR);
        lv_obj_set_style_translate_x(scale_line, 10, LV_PART_INDICATOR);
        lv_obj_set_style_length(scale_line, 15, LV_PART_INDICATOR);
        lv_obj_set_style_radial_offset(scale_line, 10, LV_PART_INDICATOR);

        /*Set the style of the minor ticks*/
        lv_obj_set_style_length(scale_line, 10, LV_PART_ITEMS);
        lv_obj_set_style_radial_offset(scale_line, 5, LV_PART_ITEMS);
        lv_obj_set_style_line_opa(scale_line, LV_OPA_50, LV_PART_ITEMS);

        lv_scale_set_label_show(scale_line, true);

        lv_scale_set_total_tick_count(scale_line, 31);
        lv_scale_set_major_tick_every(scale_line, 5);

        lv_scale_set_range(scale_line, 10, 40);

        lv_scale_set_angle_range(scale_line, 270);
        lv_scale_set_rotation(scale_line, 135);

        lv_obj_t *needle_line = lv_line_create(scale_line);

        lv_obj_set_style_line_width(needle_line, 3, LV_PART_MAIN);
        lv_obj_set_style_line_rounded(needle_line, true, LV_PART_MAIN);
        lv_scale_set_line_needle_value(scale_line, needle_line, 60, 33);
    }
};

class LvExampleScale9 : public LvglComponensBase {
public:
    /**
     * A simple horizontal scale with transforms
     */
    LvExampleScale9() = default;
    LvExampleScale9(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        lv_obj_t *scale = lv_scale_create(parent);
        lv_obj_set_size(scale, 200, 100);
        lv_scale_set_mode(scale, LV_SCALE_MODE_HORIZONTAL_BOTTOM);
        lv_obj_center(scale);

        lv_scale_set_label_show(scale, true);
        lv_obj_set_style_transform_rotation(scale, 450, LV_PART_INDICATOR);
        lv_obj_set_style_length(scale, 30, LV_PART_INDICATOR);
        lv_obj_set_style_translate_x(scale, 5, LV_PART_INDICATOR);

        lv_scale_set_total_tick_count(scale, 31);
        lv_scale_set_major_tick_every(scale, 5);

        lv_obj_set_style_length(scale, 5, LV_PART_ITEMS);
        lv_obj_set_style_length(scale, 10, LV_PART_INDICATOR);
        lv_scale_set_range(scale, 10, 40);
    }
};

class LvExampleSlider1 : public LvglComponensBase {
public:
    static void slider_event_cb(lv_event_t *e);
    inline static lv_obj_t *slider_label;

    /**
     * A default slider with a label displaying the current value
     */
    LvExampleSlider1() = default;
    LvExampleSlider1(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        /*Create a slider in the center of the display*/
        lv_obj_t *slider = lv_slider_create(parent);
        lv_obj_center(slider);
        lv_obj_add_event_cb(slider, slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

        lv_obj_set_style_anim_duration(slider, 2000, 0);
        /*Create a label below the slider*/
        slider_label = lv_label_create(parent);
        lv_label_set_text(slider_label, "0%");

        lv_obj_align_to(slider_label, slider, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
    }

    static void slider_event_cb(lv_event_t *e)
    {
        lv_obj_t *slider = lv_event_get_target(e);
        char buf[8];
        lv_snprintf(buf, sizeof(buf), "%d%%", (int)lv_slider_get_value(slider));
        lv_label_set_text(slider_label, buf);
        lv_obj_align_to(slider_label, slider, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
    }
};

class LvExampleSlider2 : public LvglComponensBase {
public:
    /**
     * Show how to style a slider.
     */
    LvExampleSlider2() = default;
    LvExampleSlider2(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        /*Create a transition*/
        static const lv_style_prop_t props[] = {LV_STYLE_BG_COLOR, 0};
        static lv_style_transition_dsc_t transition_dsc;
        lv_style_transition_dsc_init(&transition_dsc, props, lv_anim_path_linear, 300, 0, NULL);

        static lv_style_t style_main;
        static lv_style_t style_indicator;
        static lv_style_t style_knob;
        static lv_style_t style_pressed_color;
        lv_style_init(&style_main);
        lv_style_set_bg_opa(&style_main, LV_OPA_COVER);
        lv_style_set_bg_color(&style_main, lv_color_hex3(0xbbb));
        lv_style_set_radius(&style_main, LV_RADIUS_CIRCLE);
        lv_style_set_pad_ver(&style_main, -2); /*Makes the indicator larger*/

        lv_style_init(&style_indicator);
        lv_style_set_bg_opa(&style_indicator, LV_OPA_COVER);
        lv_style_set_bg_color(&style_indicator, lv_palette_main(LV_PALETTE_CYAN));
        lv_style_set_radius(&style_indicator, LV_RADIUS_CIRCLE);
        lv_style_set_transition(&style_indicator, &transition_dsc);

        lv_style_init(&style_knob);
        lv_style_set_bg_opa(&style_knob, LV_OPA_COVER);
        lv_style_set_bg_color(&style_knob, lv_palette_main(LV_PALETTE_CYAN));
        lv_style_set_border_color(&style_knob, lv_palette_darken(LV_PALETTE_CYAN, 3));
        lv_style_set_border_width(&style_knob, 2);
        lv_style_set_radius(&style_knob, LV_RADIUS_CIRCLE);
        lv_style_set_pad_all(&style_knob, 6); /*Makes the knob larger*/
        lv_style_set_transition(&style_knob, &transition_dsc);

        lv_style_init(&style_pressed_color);
        lv_style_set_bg_color(&style_pressed_color, lv_palette_darken(LV_PALETTE_CYAN, 2));

        /*Create a slider and add the style*/
        lv_obj_t *slider = lv_slider_create(parent);
        lv_obj_remove_style_all(slider); /*Remove the styles coming from the theme*/

        lv_obj_add_style(slider, &style_main, LV_PART_MAIN);
        lv_obj_add_style(slider, &style_indicator, LV_PART_INDICATOR);
        lv_obj_add_style(slider, &style_pressed_color, LV_PART_INDICATOR | LV_STATE_PRESSED);
        lv_obj_add_style(slider, &style_knob, LV_PART_KNOB);
        lv_obj_add_style(slider, &style_pressed_color, LV_PART_KNOB | LV_STATE_PRESSED);

        lv_obj_center(slider);
    }
};

class LvExampleSlider3 : public LvglComponensBase {
public:
#define MAX_VALUE 100
#define MIN_VALUE 0

    static void slider_event_cb(lv_event_t *e);

    /**
     * Show the current value when the slider is pressed by extending the drawer
     *
     */
    LvExampleSlider3() = default;
    LvExampleSlider3(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        /*Create a slider in the center of the display*/
        lv_obj_t *slider;
        slider = lv_slider_create(parent);
        lv_obj_center(slider);

        lv_slider_set_mode(slider, LV_SLIDER_MODE_RANGE);
        lv_slider_set_range(slider, MIN_VALUE, MAX_VALUE);
        lv_slider_set_value(slider, 70, LV_ANIM_OFF);
        lv_slider_set_left_value(slider, 20, LV_ANIM_OFF);

        lv_obj_add_event_cb(slider, slider_event_cb, LV_EVENT_ALL, NULL);
        lv_obj_refresh_ext_draw_size(slider);
    }

    static void slider_event_cb(lv_event_t *e)
    {
        lv_event_code_t code = lv_event_get_code(e);
        lv_obj_t *obj        = lv_event_get_target(e);

        /*Provide some extra space for the value*/
        if (code == LV_EVENT_REFR_EXT_DRAW_SIZE) {
            lv_event_set_ext_draw_size(e, 50);
        } else if (code == LV_EVENT_DRAW_MAIN_END) {
            if (!lv_obj_has_state(obj, LV_STATE_PRESSED)) return;

            lv_area_t slider_area;
            lv_obj_get_coords(obj, &slider_area);
            lv_area_t indic_area = slider_area;
            lv_area_set_width(&indic_area, lv_area_get_width(&slider_area) * lv_slider_get_value(obj) / MAX_VALUE);
            indic_area.x1 += lv_area_get_width(&slider_area) * lv_slider_get_left_value(obj) / MAX_VALUE;
            char buf[16];
            lv_snprintf(buf, sizeof(buf), "%d - %d", (int)lv_slider_get_left_value(obj), (int)lv_slider_get_value(obj));

            lv_point_t label_size;
            lv_text_get_size(&label_size, buf, LV_FONT_DEFAULT, 0, 0, LV_COORD_MAX, 0);
            lv_area_t label_area;
            label_area.x1 = 0;
            label_area.x2 = label_size.x - 1;
            label_area.y1 = 0;
            label_area.y2 = label_size.y - 1;

            lv_area_align(&indic_area, &label_area, LV_ALIGN_OUT_TOP_MID, 0, -10);

            lv_draw_label_dsc_t label_draw_dsc;
            lv_draw_label_dsc_init(&label_draw_dsc);
            label_draw_dsc.color      = lv_color_hex3(0x888);
            label_draw_dsc.text       = buf;
            label_draw_dsc.text_local = true;
            lv_layer_t *layer         = lv_event_get_layer(e);
            lv_draw_label(layer, &label_draw_dsc, &label_area);
        }
    }
};

class LvExampleSlider4 : public LvglComponensBase {
public:
    static void slider_event_cb(lv_event_t *e);
    inline static lv_obj_t *slider_label;

    /**
     * Slider with opposite direction
     */
    LvExampleSlider4() = default;
    LvExampleSlider4(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        /*Create a slider in the center of the display*/
        lv_obj_t *slider = lv_slider_create(parent);
        lv_obj_center(slider);
        lv_obj_add_event_cb(slider, slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
        /*Reverse the direction of the slider*/
        lv_slider_set_range(slider, 100, 0);
        /*Create a label below the slider*/
        slider_label = lv_label_create(parent);
        lv_label_set_text(slider_label, "0%");

        lv_obj_align_to(slider_label, slider, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
    }

    static void slider_event_cb(lv_event_t *e)
    {
        lv_obj_t *slider = lv_event_get_target(e);
        char buf[8];
        lv_snprintf(buf, sizeof(buf), "%d%%", (int)lv_slider_get_value(slider));
        lv_label_set_text(slider_label, buf);
        lv_obj_align_to(slider_label, slider, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
    }
};

class LvExampleSpan1 : public LvglComponensBase {
public:
    static void click_event_cb(lv_event_t *e)
    {
        lv_obj_t *spans   = lv_event_get_target(e);
        lv_indev_t *indev = lv_event_get_indev(e);
        lv_point_t point;
        lv_indev_get_point(indev, &point);
        lv_span_t *span = lv_spangroup_get_span_by_point(spans, &point);

        LV_LOG_USER("%s", span ? lv_span_get_text(span) : "NULL");
    }

    /**
     * Create spans and get clicked one
     */
    LvExampleSpan1() = default;
    LvExampleSpan1(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        static lv_style_t style;
        lv_style_init(&style);
        lv_style_set_border_width(&style, 1);
        lv_style_set_border_color(&style, lv_palette_main(LV_PALETTE_ORANGE));
        lv_style_set_pad_all(&style, 2);

        lv_obj_t *spans = lv_spangroup_create(parent);
        lv_obj_set_width(spans, 300);
        lv_obj_set_height(spans, 300);
        lv_obj_center(spans);
        lv_obj_add_style(spans, &style, 0);
        lv_obj_add_flag(spans, LV_OBJ_FLAG_CLICKABLE);

        lv_spangroup_set_align(spans, LV_TEXT_ALIGN_LEFT);
        lv_spangroup_set_overflow(spans, LV_SPAN_OVERFLOW_CLIP);
        lv_spangroup_set_indent(spans, 20);
        lv_spangroup_set_mode(spans, LV_SPAN_MODE_BREAK);

        lv_span_t *span = lv_spangroup_new_span(spans);
        lv_span_set_text(span, "China is a beautiful country.");
        lv_style_set_text_color(lv_span_get_style(span), lv_palette_main(LV_PALETTE_RED));
        lv_style_set_text_decor(lv_span_get_style(span), LV_TEXT_DECOR_UNDERLINE);
        lv_style_set_text_opa(lv_span_get_style(span), LV_OPA_50);

        span = lv_spangroup_new_span(spans);
        lv_span_set_text_static(span, "good good study, day day up.");
#if LV_FONT_MONTSERRAT_24
        lv_style_set_text_font(lv_span_get_style(span), &lv_font_montserrat_24);
#endif
        lv_style_set_text_color(lv_span_get_style(span), lv_palette_main(LV_PALETTE_GREEN));

        span = lv_spangroup_new_span(spans);
        lv_span_set_text_static(span, "LVGL is an open-source graphics library.");
        lv_style_set_text_color(lv_span_get_style(span), lv_palette_main(LV_PALETTE_BLUE));

        span = lv_spangroup_new_span(spans);
        lv_span_set_text_static(span, "the boy no name.");
        lv_style_set_text_color(lv_span_get_style(span), lv_palette_main(LV_PALETTE_GREEN));
#if LV_FONT_MONTSERRAT_20
        lv_style_set_text_font(lv_span_get_style(span), &lv_font_montserrat_20);
#endif
        lv_style_set_text_decor(lv_span_get_style(span), LV_TEXT_DECOR_UNDERLINE);

        span = lv_spangroup_new_span(spans);
        lv_span_set_text(span, "I have a dream that hope to come true.");
        lv_style_set_text_decor(lv_span_get_style(span), LV_TEXT_DECOR_STRIKETHROUGH);

        lv_spangroup_refr_mode(spans);

        lv_obj_add_event_cb(spans, click_event_cb, LV_EVENT_CLICKED, NULL);
    }
};

class LvExampleSpinbox1 : public LvglComponensBase {
public:
    inline static lv_obj_t *spinbox;

    static void lv_spinbox_increment_event_cb(lv_event_t *e)
    {
        lv_event_code_t code = lv_event_get_code(e);
        if (code == LV_EVENT_SHORT_CLICKED || code == LV_EVENT_LONG_PRESSED_REPEAT) {
            lv_spinbox_increment(spinbox);
        }
    }

    static void lv_spinbox_decrement_event_cb(lv_event_t *e)
    {
        lv_event_code_t code = lv_event_get_code(e);
        if (code == LV_EVENT_SHORT_CLICKED || code == LV_EVENT_LONG_PRESSED_REPEAT) {
            lv_spinbox_decrement(spinbox);
        }
    }

    LvExampleSpinbox1() = default;
    LvExampleSpinbox1(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        spinbox = lv_spinbox_create(parent);
        lv_spinbox_set_range(spinbox, -1000, 25000);
        lv_spinbox_set_digit_format(spinbox, 5, 2);
        lv_spinbox_step_prev(spinbox);
        lv_obj_set_width(spinbox, 100);
        lv_obj_center(spinbox);

        int32_t h = lv_obj_get_height(spinbox);

        lv_obj_t *btn = lv_button_create(parent);
        lv_obj_set_size(btn, h, h);
        lv_obj_align_to(btn, spinbox, LV_ALIGN_OUT_RIGHT_MID, 5, 0);
        lv_obj_set_style_bg_image_src(btn, LV_SYMBOL_PLUS, 0);
        lv_obj_add_event_cb(btn, lv_spinbox_increment_event_cb, LV_EVENT_ALL, NULL);

        btn = lv_button_create(parent);
        lv_obj_set_size(btn, h, h);
        lv_obj_align_to(btn, spinbox, LV_ALIGN_OUT_LEFT_MID, -5, 0);
        lv_obj_set_style_bg_image_src(btn, LV_SYMBOL_MINUS, 0);
        lv_obj_add_event_cb(btn, lv_spinbox_decrement_event_cb, LV_EVENT_ALL, NULL);
    }
};

class LvExampleSpinner1 : public LvglComponensBase {
public:
    LvExampleSpinner1() = default;
    LvExampleSpinner1(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        /*Create a spinner*/
        lv_obj_t *spinner = lv_spinner_create(parent);
        lv_obj_set_size(spinner, 100, 100);
        lv_obj_center(spinner);
        lv_spinner_set_anim_params(spinner, 10000, 200);
    }
};

class LvExampleSwitch1 : public LvglComponensBase {
public:
    static void event_handler(lv_event_t *e)
    {
        lv_event_code_t code = lv_event_get_code(e);
        lv_obj_t *obj        = lv_event_get_target(e);
        if (code == LV_EVENT_VALUE_CHANGED) {
            LV_UNUSED(obj);
            LV_LOG_USER("State: %s\n", lv_obj_has_state(obj, LV_STATE_CHECKED) ? "On" : "Off");
        }
    }

    LvExampleSwitch1() = default;
    LvExampleSwitch1(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t *sw;

        sw = lv_switch_create(parent);
        lv_obj_add_event_cb(sw, event_handler, LV_EVENT_ALL, NULL);
        lv_obj_add_flag(sw, LV_OBJ_FLAG_EVENT_BUBBLE);

        sw = lv_switch_create(parent);
        lv_obj_add_state(sw, LV_STATE_CHECKED);
        lv_obj_add_event_cb(sw, event_handler, LV_EVENT_ALL, NULL);

        sw = lv_switch_create(parent);
        lv_obj_add_state(sw, LV_STATE_DISABLED);
        lv_obj_add_event_cb(sw, event_handler, LV_EVENT_ALL, NULL);

        sw = lv_switch_create(parent);
        lv_obj_add_state(sw, LV_STATE_CHECKED | LV_STATE_DISABLED);
        lv_obj_add_event_cb(sw, event_handler, LV_EVENT_ALL, NULL);
    }
};

class LvExampleSwitch2 : public LvglComponensBase {
public:
    static void event_handler(lv_event_t *e)
    {
        lv_event_code_t code = lv_event_get_code(e);
        lv_obj_t *obj        = lv_event_get_target(e);
        if (code == LV_EVENT_VALUE_CHANGED) {
            LV_UNUSED(obj);
            LV_LOG_USER("State: %s\n", lv_obj_has_state(obj, LV_STATE_CHECKED) ? "On" : "Off");
        }
    }

    LvExampleSwitch2() = default;
    LvExampleSwitch2(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t *sw;

        sw = lv_switch_create(parent);
        lv_obj_set_size(sw, 30, 60);
        lv_obj_add_event_cb(sw, event_handler, LV_EVENT_ALL, NULL);

        sw = lv_switch_create(parent);
        lv_obj_set_size(sw, 30, 60);
        lv_switch_set_orientation(sw, LV_SWITCH_ORIENTATION_VERTICAL);
        lv_obj_add_state(sw, LV_STATE_CHECKED);
        lv_obj_add_event_cb(sw, event_handler, LV_EVENT_ALL, NULL);
    }
};

class LvExampleTable1 : public LvglComponensBase {
public:
    static void draw_event_cb(lv_event_t *e)
    {
        lv_draw_task_t *draw_task    = lv_event_get_draw_task(e);
        lv_draw_dsc_base_t *base_dsc = lv_draw_task_get_draw_dsc(draw_task);
        /*If the cells are drawn...*/
        if (base_dsc->part == LV_PART_ITEMS) {
            uint32_t row = base_dsc->id1;
            uint32_t col = base_dsc->id2;

            /*Make the texts in the first cell center aligned*/
            if (row == 0) {
                lv_draw_label_dsc_t *label_draw_dsc = lv_draw_task_get_label_dsc(draw_task);
                if (label_draw_dsc) {
                    label_draw_dsc->align = LV_TEXT_ALIGN_CENTER;
                }
                lv_draw_fill_dsc_t *fill_draw_dsc = lv_draw_task_get_fill_dsc(draw_task);
                if (fill_draw_dsc) {
                    fill_draw_dsc->color =
                        lv_color_mix(lv_palette_main(LV_PALETTE_BLUE), fill_draw_dsc->color, LV_OPA_20);
                    fill_draw_dsc->opa = LV_OPA_COVER;
                }
            }
            /*In the first column align the texts to the right*/
            else if (col == 0) {
                lv_draw_label_dsc_t *label_draw_dsc = lv_draw_task_get_label_dsc(draw_task);
                if (label_draw_dsc) {
                    label_draw_dsc->align = LV_TEXT_ALIGN_RIGHT;
                }
            }

            /*Make every 2nd row grayish*/
            if ((row != 0 && row % 2) == 0) {
                lv_draw_fill_dsc_t *fill_draw_dsc = lv_draw_task_get_fill_dsc(draw_task);
                if (fill_draw_dsc) {
                    fill_draw_dsc->color =
                        lv_color_mix(lv_palette_main(LV_PALETTE_GREY), fill_draw_dsc->color, LV_OPA_10);
                    fill_draw_dsc->opa = LV_OPA_COVER;
                }
            }
        }
    }

    LvExampleTable1() = default;
    LvExampleTable1(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        lv_obj_t *table = lv_table_create(parent);

        /*Fill the first column*/
        lv_table_set_cell_value(table, 0, 0, "Name");
        lv_table_set_cell_value(table, 1, 0, "Apple");
        lv_table_set_cell_value(table, 2, 0, "Banana");
        lv_table_set_cell_value(table, 3, 0, "Lemon");
        lv_table_set_cell_value(table, 4, 0, "Grape");
        lv_table_set_cell_value(table, 5, 0, "Melon");
        lv_table_set_cell_value(table, 6, 0, "Peach");
        lv_table_set_cell_value(table, 7, 0, "Nuts");

        /*Fill the second column*/
        lv_table_set_cell_value(table, 0, 1, "Price");
        lv_table_set_cell_value(table, 1, 1, "$7");
        lv_table_set_cell_value(table, 2, 1, "$4");
        lv_table_set_cell_value(table, 3, 1, "$6");
        lv_table_set_cell_value(table, 4, 1, "$2");
        lv_table_set_cell_value(table, 5, 1, "$5");
        lv_table_set_cell_value(table, 6, 1, "$1");
        lv_table_set_cell_value(table, 7, 1, "$9");

        /*Set a smaller height to the table. It'll make it scrollable*/
        lv_obj_set_height(table, 200);
        lv_obj_center(table);

        /*Add an event callback to to apply some custom drawing*/
        lv_obj_add_event_cb(table, draw_event_cb, LV_EVENT_DRAW_TASK_ADDED, NULL);
        lv_obj_add_flag(table, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);
    }
};

class LvExampleTable2 : public LvglComponensBase {
public:
#define ITEM_CNT 200

    static void draw_event_cb(lv_event_t *e)
    {
        lv_obj_t *obj = lv_event_get_target(e);

        lv_draw_task_t *draw_task    = lv_event_get_draw_task(e);
        lv_draw_dsc_base_t *base_dsc = lv_draw_task_get_draw_dsc(draw_task);
        /*If the cells are drawn...*/
        if (base_dsc->part == LV_PART_ITEMS && lv_draw_task_get_type(draw_task) == LV_DRAW_TASK_TYPE_FILL) {
            /*Draw the background*/
            bool chk = lv_table_has_cell_ctrl(obj, base_dsc->id1, 0, LV_TABLE_CELL_CTRL_CUSTOM_1);
            lv_draw_rect_dsc_t rect_dsc;
            lv_draw_rect_dsc_init(&rect_dsc);
            rect_dsc.bg_color = chk ? lv_theme_get_color_primary(obj) : lv_palette_lighten(LV_PALETTE_GREY, 2);
            rect_dsc.radius   = LV_RADIUS_CIRCLE;

            lv_area_t sw_area;
            sw_area.x1 = 0;
            sw_area.x2 = 40;
            sw_area.y1 = 0;
            sw_area.y2 = 24;
            lv_area_t draw_task_area;
            lv_draw_task_get_area(draw_task, &draw_task_area);
            lv_area_align(&draw_task_area, &sw_area, LV_ALIGN_RIGHT_MID, -15, 0);
            lv_draw_rect(base_dsc->layer, &rect_dsc, &sw_area);

            /*Draw the knob*/
            rect_dsc.bg_color = lv_color_white();
            lv_area_t knob_area;
            knob_area.x1 = 0;
            knob_area.x2 = 18;
            knob_area.y1 = 0;
            knob_area.y2 = 18;
            if (chk) {
                lv_area_align(&sw_area, &knob_area, LV_ALIGN_RIGHT_MID, -3, 0);
            } else {
                lv_area_align(&sw_area, &knob_area, LV_ALIGN_LEFT_MID, 3, 0);
            }
            lv_draw_rect(base_dsc->layer, &rect_dsc, &knob_area);
        }
    }

    static void change_event_cb(lv_event_t *e)
    {
        lv_obj_t *obj = lv_event_get_target(e);
        uint32_t col;
        uint32_t row;
        lv_table_get_selected_cell(obj, &row, &col);
        bool chk = lv_table_has_cell_ctrl(obj, row, 0, LV_TABLE_CELL_CTRL_CUSTOM_1);
        if (chk)
            lv_table_clear_cell_ctrl(obj, row, 0, LV_TABLE_CELL_CTRL_CUSTOM_1);
        else
            lv_table_add_cell_ctrl(obj, row, 0, LV_TABLE_CELL_CTRL_CUSTOM_1);
    }

    /**
     * A very light-weighted list created from table
     */
    LvExampleTable2() = default;
    LvExampleTable2(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        /*Measure memory usage*/
        lv_mem_monitor_t mon1;
        lv_mem_monitor(&mon1);

        uint32_t t = lv_tick_get();

        lv_obj_t *table = lv_table_create(parent);

        /*Set a smaller height to the table. It'll make it scrollable*/
        lv_obj_set_size(table, LV_SIZE_CONTENT, 200);

        lv_table_set_column_width(table, 0, 150);
        lv_table_set_row_count(
            table, ITEM_CNT); /*Not required but avoids a lot of memory reallocation lv_table_set_set_value*/
        lv_table_set_column_count(table, 1);

        /*Don't make the cell pressed, we will draw something different in the event*/
        lv_obj_remove_style(table, NULL, LV_PART_ITEMS | LV_STATE_PRESSED);

        uint32_t i;
        for (i = 0; i < ITEM_CNT; i++) {
            lv_table_set_cell_value_fmt(table, i, 0, "Item %" LV_PRIu32, i + 1);
        }

        lv_obj_align(table, LV_ALIGN_CENTER, 0, -20);

        /*Add an event callback to to apply some custom drawing*/
        lv_obj_add_event_cb(table, draw_event_cb, LV_EVENT_DRAW_TASK_ADDED, NULL);
        lv_obj_add_event_cb(table, change_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
        lv_obj_add_flag(table, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);

        lv_mem_monitor_t mon2;
        lv_mem_monitor(&mon2);

        size_t mem_used = mon1.free_size - mon2.free_size;

        uint32_t elaps = lv_tick_elaps(t);

        lv_obj_t *label = lv_label_create(parent);
        lv_label_set_text_fmt(label,
                              "%" LV_PRIu32 " items were created in %" LV_PRIu32
                              " ms\n"
                              "using %zu bytes of memory",
                              (uint32_t)ITEM_CNT, elaps, mem_used);

        lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -10);
    }
};

class LvExampleTabview1 : public LvglComponensBase {
public:
    LvExampleTabview1() = default;
    LvExampleTabview1(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        /*Create a Tab view object*/
        lv_obj_t *tabview;
        tabview = lv_tabview_create(parent);

        /*Add 3 tabs (the tabs are page (lv_page) and can be scrolled*/
        lv_obj_t *tab1 = lv_tabview_add_tab(tabview, "Tab 1");
        lv_obj_t *tab2 = lv_tabview_add_tab(tabview, "Tab 2");
        lv_obj_t *tab3 = lv_tabview_add_tab(tabview, "Tab 3");

        /*Add content to the tabs*/
        lv_obj_t *label = lv_label_create(tab1);
        lv_label_set_text(label,
                          "This the first tab\n\n"
                          "If the content\n"
                          "of a tab\n"
                          "becomes too\n"
                          "longer\n"
                          "than the\n"
                          "container\n"
                          "then it\n"
                          "automatically\n"
                          "becomes\n"
                          "scrollable.\n"
                          "\n"
                          "\n"
                          "\n"
                          "Can you see it?");

        label = lv_label_create(tab2);
        lv_label_set_text(label, "Second tab");

        label = lv_label_create(tab3);
        lv_label_set_text(label, "Third tab");

        lv_obj_scroll_to_view_recursive(label, LV_ANIM_ON);
    }
};

class LvExampleTabview2 : public LvglComponensBase {
public:
    /*A vertical tab view with disabled scrolling and some styling*/
    LvExampleTabview2() = default;
    LvExampleTabview2(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        /*Create a Tab view object*/
        lv_obj_t *tabview;
        tabview = lv_tabview_create(parent);
        lv_tabview_set_tab_bar_position(tabview, LV_DIR_LEFT);
        lv_tabview_set_tab_bar_size(tabview, 80);

        lv_obj_set_style_bg_color(tabview, lv_palette_lighten(LV_PALETTE_RED, 2), 0);

        lv_obj_t *tab_buttons = lv_tabview_get_tab_bar(tabview);
        lv_obj_set_style_bg_color(tab_buttons, lv_palette_darken(LV_PALETTE_GREY, 3), 0);
        lv_obj_set_style_text_color(tab_buttons, lv_palette_lighten(LV_PALETTE_GREY, 5), 0);
        lv_obj_set_style_border_side(tab_buttons, LV_BORDER_SIDE_RIGHT, LV_PART_ITEMS | LV_STATE_CHECKED);

        /*Add 3 tabs (the tabs are page (lv_page) and can be scrolled*/
        lv_obj_t *tab1 = lv_tabview_add_tab(tabview, "Tab 1");
        lv_obj_t *tab2 = lv_tabview_add_tab(tabview, "Tab 2");
        lv_obj_t *tab3 = lv_tabview_add_tab(tabview, "Tab 3");
        lv_obj_t *tab4 = lv_tabview_add_tab(tabview, "Tab 4");
        lv_obj_t *tab5 = lv_tabview_add_tab(tabview, "Tab 5");

        lv_obj_set_style_bg_color(tab2, lv_palette_lighten(LV_PALETTE_AMBER, 3), 0);
        lv_obj_set_style_bg_opa(tab2, LV_OPA_COVER, 0);

        /*Add content to the tabs*/
        lv_obj_t *label = lv_label_create(tab1);
        lv_label_set_text(label, "First tab");

        label = lv_label_create(tab2);
        lv_label_set_text(label, "Second tab");

        label = lv_label_create(tab3);
        lv_label_set_text(label, "Third tab");

        label = lv_label_create(tab4);
        lv_label_set_text(label, "Forth tab");

        label = lv_label_create(tab5);
        lv_label_set_text(label, "Fifth tab");

        lv_obj_remove_flag(lv_tabview_get_content(tabview), LV_OBJ_FLAG_SCROLLABLE);
    }
};

class LvExampleTextarea1 : public LvglComponensBase {
public:
    static void textarea_event_handler(lv_event_t *e)
    {
        lv_obj_t *ta = lv_event_get_target(e);
        LV_UNUSED(ta);
        LV_LOG_USER("Enter was pressed. The current text is: %s", lv_textarea_get_text(ta));
    }

    static void btnm_event_handler(lv_event_t *e)
    {
        lv_obj_t *obj   = lv_event_get_target(e);
        lv_obj_t *ta    = lv_event_get_user_data(e);
        const char *txt = lv_buttonmatrix_get_button_text(obj, lv_buttonmatrix_get_selected_button(obj));

        if (lv_strcmp(txt, LV_SYMBOL_BACKSPACE) == 0)
            lv_textarea_delete_char(ta);
        else if (lv_strcmp(txt, LV_SYMBOL_NEW_LINE) == 0)
            lv_obj_send_event(ta, LV_EVENT_READY, NULL);
        else
            lv_textarea_add_text(ta, txt);
    }

    LvExampleTextarea1() = default;
    LvExampleTextarea1(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        lv_obj_t *ta = lv_textarea_create(parent);
        lv_textarea_set_one_line(ta, true);
        lv_obj_align(ta, LV_ALIGN_TOP_MID, 0, 10);
        lv_obj_add_event_cb(ta, textarea_event_handler, LV_EVENT_READY, ta);
        lv_obj_add_state(ta, LV_STATE_FOCUSED); /*To be sure the cursor is visible*/

        static const char *btnm_map[] = {
            "1", "2", "3", "\n", "4", "5", "6", "\n", "7", "8", "9", "\n", LV_SYMBOL_BACKSPACE, "0", LV_SYMBOL_NEW_LINE,
            ""};

        lv_obj_t *btnm = lv_buttonmatrix_create(parent);
        lv_obj_set_size(btnm, 200, 150);
        lv_obj_align(btnm, LV_ALIGN_BOTTOM_MID, 0, -10);
        lv_obj_add_event_cb(btnm, btnm_event_handler, LV_EVENT_VALUE_CHANGED, ta);
        lv_obj_remove_flag(btnm, LV_OBJ_FLAG_CLICK_FOCUSABLE); /*To keep the text area focused on button clicks*/
        lv_buttonmatrix_set_map(btnm, btnm_map);
    }
};

class LvExampleTextarea2 : public LvglComponensBase {
public:
    static void ta_event_cb(lv_event_t *e);

    inline static lv_obj_t *kb;

    LvExampleTextarea2() = default;
    LvExampleTextarea2(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        /*Create the password box*/
        lv_obj_t *pwd_ta = lv_textarea_create(parent);
        lv_textarea_set_text(pwd_ta, "");
        lv_textarea_set_password_mode(pwd_ta, true);
        lv_textarea_set_one_line(pwd_ta, true);
        lv_obj_set_width(pwd_ta, lv_pct(40));
        lv_obj_set_pos(pwd_ta, 5, 20);
        lv_obj_add_event_cb(pwd_ta, ta_event_cb, LV_EVENT_ALL, NULL);

        /*Create a label and position it above the text box*/
        lv_obj_t *pwd_label = lv_label_create(parent);
        lv_label_set_text(pwd_label, "Password:");
        lv_obj_align_to(pwd_label, pwd_ta, LV_ALIGN_OUT_TOP_LEFT, 0, 0);

        /*Create the one-line mode text area*/
        lv_obj_t *text_ta = lv_textarea_create(parent);
        lv_textarea_set_one_line(text_ta, true);
        lv_textarea_set_password_mode(text_ta, false);
        lv_obj_set_width(text_ta, lv_pct(40));
        lv_obj_add_event_cb(text_ta, ta_event_cb, LV_EVENT_ALL, NULL);
        lv_obj_align(text_ta, LV_ALIGN_TOP_RIGHT, -5, 20);

        /*Create a label and position it above the text box*/
        lv_obj_t *oneline_label = lv_label_create(parent);
        lv_label_set_text(oneline_label, "Text:");
        lv_obj_align_to(oneline_label, text_ta, LV_ALIGN_OUT_TOP_LEFT, 0, 0);

        /*Create a keyboard*/
        kb = lv_keyboard_create(parent);
        lv_obj_set_size(kb, LV_HOR_RES, LV_VER_RES / 2);

        lv_keyboard_set_textarea(kb, pwd_ta); /*Focus it on one of the text areas to start*/

        /*The keyboard will show Arabic characters if they are enabled */
#if LV_USE_ARABIC_PERSIAN_CHARS && LV_FONT_DEJAVU_16_PERSIAN_HEBREW
        lv_obj_set_style_text_font(kb, &lv_font_dejavu_16_persian_hebrew, 0);
        lv_obj_set_style_text_font(text_ta, &lv_font_dejavu_16_persian_hebrew, 0);
        lv_obj_set_style_text_font(pwd_ta, &lv_font_dejavu_16_persian_hebrew, 0);
#endif
    }

    static void ta_event_cb(lv_event_t *e)
    {
        lv_event_code_t code = lv_event_get_code(e);
        lv_obj_t *ta         = lv_event_get_target(e);
        if (code == LV_EVENT_CLICKED || code == LV_EVENT_FOCUSED) {
            /*Focus on the clicked text area*/
            if (kb != NULL) lv_keyboard_set_textarea(kb, ta);
        }

        else if (code == LV_EVENT_READY) {
            LV_LOG_USER("Ready, current text: %s", lv_textarea_get_text(ta));
        }
    }
};

class LvExampleTextarea3 : public LvglComponensBase {
public:
    static void ta_event_cb(lv_event_t *e);

    inline static lv_obj_t *kb;

    /**
     * Automatically format text like a clock. E.g. "12:34"
     * Add the ':' automatically.
     */
    LvExampleTextarea3() = default;
    LvExampleTextarea3(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        /*Create the text area*/
        lv_obj_t *ta = lv_textarea_create(parent);
        lv_obj_add_event_cb(ta, ta_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
        lv_textarea_set_accepted_chars(ta, "0123456789:");
        lv_textarea_set_max_length(ta, 5);
        lv_textarea_set_one_line(ta, true);
        lv_textarea_set_text(ta, "");

        /*Create a keyboard*/
        kb = lv_keyboard_create(parent);
        lv_obj_set_size(kb, LV_HOR_RES, LV_VER_RES / 2);
        lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_NUMBER);
        lv_keyboard_set_textarea(kb, ta);
    }

    static void ta_event_cb(lv_event_t *e)
    {
        lv_obj_t *ta    = lv_event_get_target(e);
        const char *txt = lv_textarea_get_text(ta);
        if (txt[0] >= '0' && txt[0] <= '9' && txt[1] >= '0' && txt[1] <= '9' && txt[2] != ':') {
            lv_textarea_set_cursor_pos(ta, 2);
            lv_textarea_add_char(ta, ':');
        }
    }
};

class LvExampleTileview1 : public LvglComponensBase {
public:
    /**
     * Create a 2x2 tile view and allow scrolling only in an "L" shape.
     * Demonstrate scroll chaining with a long list that
     * scrolls the tile view when it can't be scrolled further.
     */
    LvExampleTileview1() = default;
    LvExampleTileview1(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        lv_obj_t *tv = lv_tileview_create(parent);

        /*Tile1: just a label*/
        lv_obj_t *tile1 = lv_tileview_add_tile(tv, 0, 0, LV_DIR_BOTTOM);
        lv_obj_t *label = lv_label_create(tile1);
        lv_label_set_text(label, "Scroll down");
        lv_obj_center(label);

        /*Tile2: a button*/
        lv_obj_t *tile2 = lv_tileview_add_tile(tv, 0, 1, LV_DIR_TOP | LV_DIR_RIGHT);

        lv_obj_t *btn = lv_button_create(tile2);

        label = lv_label_create(btn);
        lv_label_set_text(label, "Scroll up or right");

        lv_obj_set_size(btn, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_center(btn);

        /*Tile3: a list*/
        lv_obj_t *tile3 = lv_tileview_add_tile(tv, 1, 1, LV_DIR_LEFT);
        lv_obj_t *list  = lv_list_create(tile3);
        lv_obj_set_size(list, LV_PCT(100), LV_PCT(100));

        lv_list_add_button(list, NULL, "One");
        lv_list_add_button(list, NULL, "Two");
        lv_list_add_button(list, NULL, "Three");
        lv_list_add_button(list, NULL, "Four");
        lv_list_add_button(list, NULL, "Five");
        lv_list_add_button(list, NULL, "Six");
        lv_list_add_button(list, NULL, "Seven");
        lv_list_add_button(list, NULL, "Eight");
        lv_list_add_button(list, NULL, "Nine");
        lv_list_add_button(list, NULL, "Ten");
    }
};

class LvExampleWin1 : public LvglComponensBase {
public:
    static void event_handler(lv_event_t *e)
    {
        lv_obj_t *obj = lv_event_get_target(e);
        LV_UNUSED(obj);
        LV_LOG_USER("Button %d clicked", (int)lv_obj_get_index(obj));
    }

    LvExampleWin1() = default;
    LvExampleWin1(lv_obj_t *parent)
    {
        create_ui(parent);
    }
    void create_ui(lv_obj_t *parent) override
    {
        lv_obj_t *win = lv_win_create(parent);
        lv_obj_t *btn;
        btn = lv_win_add_button(win, LV_SYMBOL_LEFT, 40);
        lv_obj_add_event_cb(btn, event_handler, LV_EVENT_CLICKED, NULL);

        lv_win_add_title(win, "A title");

        btn = lv_win_add_button(win, LV_SYMBOL_RIGHT, 40);
        lv_obj_add_event_cb(btn, event_handler, LV_EVENT_CLICKED, NULL);

        btn = lv_win_add_button(win, LV_SYMBOL_CLOSE, 60);
        lv_obj_add_event_cb(btn, event_handler, LV_EVENT_CLICKED, NULL);

        lv_obj_t *cont  = lv_win_get_content(win); /*Content can be added here*/
        lv_obj_t *label = lv_label_create(cont);
        lv_label_set_text(label,
                          "This is\n"
                          "a pretty\n"
                          "long text\n"
                          "to see how\n"
                          "the window\n"
                          "becomes\n"
                          "scrollable.\n"
                          "\n"
                          "\n"
                          "Some more\n"
                          "text to be\n"
                          "sure it\n"
                          "overflows. :)");
    }
};
#endif

class LvSettingValuePage3Base : public DComponens::LvglComponensBase {
public:
    struct ActivationSink {
        AsyncToken dispatch_token;
        std::weak_ptr<SettingRequestState> request_state;
        uint64_t generation = 0;
        int32_t index = -1;
        LvSettingValuePage3Base *owner = nullptr;

        bool valid() const noexcept
        {
            auto state = request_state.lock();
            return owner != nullptr && state && state->is_current(generation) && state->pending() &&
                   dispatch_token.valid();
        }
    };

    enum class LayoutMetric : int {
        ScreenW         = 320,
        ScreenH         = 150,
        RowH            = 21,
        CenterRow       = 3,
        EdgePadding     = RowH * CenterRow,
        BarX            = 4,
        BarY            = 66,
        BarW            = 312,
        BarH            = 22,
        TitleCenterX    = 60,
        TitleBoxW       = 84,
        ValueListX      = 100,
        ValueListW      = 120,
        ValueCenterX    = ValueListW / 2,
        ValueBoxX       = 16,
        ValueBoxW       = 88,
        RightArrowScale = 224,
    };

    static constexpr int metric(LayoutMetric value)
    {
        return static_cast<int>(value);
    }

    int32_t selected_index = 0;

    LvSettingValuePage3Base() = default;

    bool activation_pending() const noexcept
    {
        return activation_pending_;
    }

    SettingComponentState activation_state() const noexcept
    {
        auto state = activation_state_.lock();
        return state ? state->state() : SettingComponentState::Read;
    }

    ActivationSink activation_sink() const noexcept
    {
        return activation_sink_;
    }

    static bool enqueue_activation_result(const ActivationSink &sink, SettingApiResult result) noexcept
    {
        if (!sink.valid()) return false;

        return SettingsAsync::Dispatch::enqueue_from_callback(
            sink.dispatch_token,
            [sink, result] {
                auto state = sink.request_state.lock();
                if (!state || !sink.owner || !state->is_current(sink.generation) ||
                    !state->pending() || sink.owner->activation_index_ != sink.index)
                    return;
                sink.owner->finish_activation(sink, result);
            });
    }

    void AnimateNextIn(std::function<void()> animate_over_func) override
    {
        if (animate_over_func) animate_over_func();
    }
    void AnimateNextOut(std::function<void()> animate_over_func) override
    {
        if (animate_over_func) animate_over_func();
    }
    void LoadNextPage() override
    {
    }
    void LeaveNextPage() override
    {
        if (LeaveSelfPage) LeaveSelfPage();
    }

    ~LvSettingValuePage3Base() override
    {
        cancel_activation(false);
        cancel_async_tasks();
        if (ComponensObj) {
            lv_anim_del(ComponensObj, nullptr);
            lv_obj_delete(ComponensObj);
            ComponensObj = nullptr;
        }
        selection_bg_ = nullptr;
        value_list_   = nullptr;
        title_label_  = nullptr;
        right_arrow_  = nullptr;
        arrow_up_     = nullptr;
        arrow_down_   = nullptr;
        hint_         = nullptr;
    }

    static void style_value_label(lv_obj_t *label, int distance)
    {
        if (!label) return;

        int font_size  = 10;
        int opa        = 130;
        uint32_t color = 0x555555;
        if (distance == 0) {
            font_size = 16;
            opa       = 255;
            color     = 0xFFFFFF;
        } else if (distance == 1) {
            font_size = 12;
            opa       = 220;
            color     = 0xAAAAAA;
        } else if (distance == 2) {
            font_size = 12;
            opa       = 170;
            color     = 0x777777;
        }

        lv_obj_set_style_text_font(
            label, cp0_fonts().get("Montserrat-Bold.ttf", font_size, LV_FREETYPE_FONT_STYLE_BOLD), LV_PART_MAIN);
        lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
        lv_obj_set_style_opa(label, opa, LV_PART_MAIN);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
        lv_obj_set_width(label, LV_SIZE_CONTENT);
        lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
        lv_obj_update_layout(label);

        const bool focused      = distance == 0;
        const int natural_width = lv_obj_get_width(label);
        if (natural_width > metric(LayoutMetric::ValueBoxW)) {
            lv_obj_set_width(label, metric(LayoutMetric::ValueBoxW));
            lv_label_set_long_mode(label, focused ? LV_LABEL_LONG_SCROLL_CIRCULAR : LV_LABEL_LONG_CLIP);
            lv_obj_set_x(label, metric(LayoutMetric::ValueBoxX));
        } else {
            lv_obj_set_width(label, LV_SIZE_CONTENT);
            lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
            lv_obj_update_layout(label);
            lv_obj_set_x(label, metric(LayoutMetric::ValueCenterX) - lv_obj_get_width(label) / 2);
        }

        const int label_y = (metric(LayoutMetric::RowH) - lv_obj_get_height(label)) / 2;
        lv_obj_set_y(label, std::max(0, label_y));
    }

    void update_value_styles()
    {
        int index = 0;
        for (lv_obj_t *row : value_rows_) {
            if (!row) {
                ++index;
                continue;
            }
            lv_obj_t *label = lv_obj_get_child(row, 0);
            style_value_label(label, std::abs(index - selected_index));
            ++index;
        }
        update_right_arrow_position();
    }

    void update_right_arrow_position()
    {
        if (!right_arrow_ || !title_label_ || item_count_ == 0) return;

        lv_obj_t *row   = row_at(selected_index);
        lv_obj_t *label = row ? lv_obj_get_child(row, 0) : nullptr;
        if (!label) return;

        lv_obj_update_layout(label);
        lv_obj_update_layout(right_arrow_);

        const int title_right = lv_obj_get_x(title_label_) + lv_obj_get_width(title_label_);
        const int value_left  = metric(LayoutMetric::ValueListX) + lv_obj_get_x(label);
        const int arrow_width = lv_obj_get_width(right_arrow_);
        const int arrow_x     = std::max(title_right + 4, value_left - 4 - arrow_width);
        const int arrow_y =
            metric(LayoutMetric::BarY) + (metric(LayoutMetric::BarH) - lv_obj_get_height(right_arrow_)) / 2;
        lv_obj_set_pos(right_arrow_, arrow_x, std::max(0, arrow_y));
        lv_obj_move_to_index(right_arrow_, 1);
    }

    void update_title_position()
    {
        if (!title_label_) return;

        lv_obj_set_width(title_label_, LV_SIZE_CONTENT);
        lv_label_set_long_mode(title_label_, LV_LABEL_LONG_CLIP);
        lv_obj_update_layout(title_label_);

        if (lv_obj_get_width(title_label_) > metric(LayoutMetric::TitleBoxW)) {
            lv_obj_set_width(title_label_, metric(LayoutMetric::TitleBoxW));
            lv_obj_set_x(title_label_,
                         metric(LayoutMetric::TitleCenterX) - metric(LayoutMetric::TitleBoxW) / 2);
        } else {
            lv_obj_set_x(title_label_,
                         metric(LayoutMetric::TitleCenterX) - lv_obj_get_width(title_label_) / 2);
        }

        const int label_y = metric(LayoutMetric::BarY) +
                            (metric(LayoutMetric::BarH) - lv_obj_get_height(title_label_)) / 2;
        lv_obj_set_y(title_label_, std::max(0, label_y));
    }

    void update_arrow_visibility()
    {
        if (arrow_up_) {
            if (selected_index > 0)
                lv_obj_remove_flag(arrow_up_, LV_OBJ_FLAG_HIDDEN);
            else
                lv_obj_add_flag(arrow_up_, LV_OBJ_FLAG_HIDDEN);
        }
        if (arrow_down_) {
            if (selected_index + 1 < static_cast<int32_t>(item_count_))
                lv_obj_remove_flag(arrow_down_, LV_OBJ_FLAG_HIDDEN);
            else
                lv_obj_add_flag(arrow_down_, LV_OBJ_FLAG_HIDDEN);
        }
    }

    void scroll_to_selected(bool animated)
    {
        if (!value_list_ || item_count_ == 0) return;

        lv_obj_t *row = row_at(selected_index);
        if (!row) return;

        lv_obj_scroll_to_view(row, animated ? LV_ANIM_ON : LV_ANIM_OFF);
        update_value_styles();
        update_arrow_visibility();
    }

    void select(int index)
    {
        if (item_count_ == 0) return;
        selected_index = std::clamp(index, 0, static_cast<int>(item_count_ - 1));
        scroll_to_selected(false);
    }

    lv_obj_t *row_at(int index) const
    {
        if (index < 0 || index >= static_cast<int>(value_rows_.size())) return nullptr;
        auto row = std::next(value_rows_.begin(), index);
        return row == value_rows_.end() ? nullptr : *row;
    }

    void restore_activation_focus()
    {
        if (!ComponensObj) return;
        lv_group_t *group = lv_obj_get_group(ComponensObj);
        if (group) lv_group_focus_obj(ComponensObj);
    }

    void finish_activation(const ActivationSink &sink, SettingApiResult result)
    {
        if (sink.owner != this || sink.index != activation_index_) return;
        auto state = activation_state_.lock();
        if (!state || state != sink.request_state.lock() || !state->is_current(sink.generation) ||
            !state->pending())
            return;
        apply_activation_result(sink, result);
    }

    void apply_activation_result(const ActivationSink &sink, SettingApiResult result)
    {
        auto state = activation_state_.lock();
        if (!state || state != sink.request_state.lock() || !state->is_current(sink.generation)) return;

        if (result == SettingApiResult::Pending) {
            if (state->mark_pending(sink.generation)) {
                activation_pending_ = true;
            } else {
                state->mark_failure(sink.generation);
                activation_pending_    = false;
                activation_index_       = -1;
                activation_generation_  = 0;
                restore_activation_focus();
            }
            return;
        }

        if (result == SettingApiResult::Success) {
            if (!state->mark_success(sink.generation)) return;
            activation_pending_    = false;
            activation_index_      = -1;
            activation_generation_ = 0;
            restore_activation_focus();
            if (LeaveSelfPage) LeaveSelfPage();
            return;
        }

        if (result == SettingApiResult::Cancelled)
            state->mark_cancelled(sink.generation);
        else if (result == SettingApiResult::Failure)
            state->mark_failure(sink.generation);
        else
            state->mark_read();

        activation_pending_    = false;
        activation_index_      = -1;
        activation_generation_ = 0;
        restore_activation_focus();
    }

    void cancel_activation(bool restore_focus = true)
    {
        auto state = activation_state_.lock();
        if (state && activation_index_ >= 0 && state->is_current(activation_generation_))
            state->mark_cancelled(activation_generation_);
        activation_pending_    = false;
        activation_index_      = -1;
        activation_generation_ = 0;
        if (restore_focus) restore_activation_focus();
    }

protected:
    LvSettingValuePage3Base(const NodeIter &parent_node, std::function<void()> back_callback)
        : parent_node_(parent_node)
    {
        LeaveSelfPage = std::move(back_callback);
    }

    void initialize(lv_obj_t *parent)
    {
        create_ui(parent);
    }

    const NodeIter &parent_node() const
    {
        return parent_node_;
    }

    virtual int initial_selection() const = 0;

    virtual SettingApiResult activate_selected()
    {
        if (item_count_ == 0 || activation_pending_ || selected_index < 0 ||
            selected_index >= static_cast<int32_t>(item_count_))
            return SettingApiResult::NotHandled;

        // The page node outlives this short-lived page3 object.  Store only
        // after an explicit OK/ENTER so ESC/LEFT keeps the previous value.
        parent_node_->selected_index = selected_index;
        auto selected_node = std::next(parent_node_.begin(), selected_index);
        if (!selected_node->has_api()) return SettingApiResult::NotHandled;

        const auto request_state = selected_node->request_state;
        const uint64_t generation = request_state->begin_activation();
        if (generation == 0) {
            activation_state_ = request_state;
            restore_activation_focus();
            return SettingApiResult::Pending;
        }

        activation_state_      = request_state;
        activation_generation_ = generation;
        activation_index_      = selected_index;

        if (!ensure_async_dispatch()) {
            request_state->mark_failure(generation);
            activation_generation_ = 0;
            activation_index_      = -1;
            restore_activation_focus();
            return SettingApiResult::Failure;
        }

        activation_pending_     = true;
        activation_sink_        = ActivationSink{async_token(), activation_state_, generation, selected_index, this};
        if (!activation_sink_.valid()) {
            request_state->mark_failure(generation);
            activation_pending_     = false;
            activation_generation_ = 0;
            activation_index_      = -1;
            restore_activation_focus();
            return SettingApiResult::Failure;
        }

        SettingApiResult result = SettingApiResult::NotHandled;
        try {
            if (selected_node->Async_api) {
                result = selected_node->Async_api(SettingApiActivate, this);
            } else if (selected_node->Componens_api) {
                selected_node->Componens_api(SettingApiActivate, this);
                result = selected_node->activation_policy == SettingActivationPolicy::WaitForResult
                             ? SettingApiResult::Pending
                             : SettingApiResult::Success;
            }
        } catch (...) {
            result = SettingApiResult::Failure;
        }

        apply_activation_result(activation_sink_, result);
        return result;
    }

    void handle_key_event(lv_event_t *event)
    {
        if (!event || lv_event_get_code(event) != LV_EVENT_KEY) return;

        const uint32_t key = lv_event_get_key(event);
        if (key == LV_KEY_ESC || key == LV_KEY_LEFT) {
            if (activation_pending_) cancel_activation();
            if (LeaveSelfPage) LeaveSelfPage();
            lv_event_stop_processing(event);
            return;
        }

        if (key == LV_KEY_UP) {
            if (selected_index > 0) {
                --selected_index;
                scroll_to_selected(true);
            }
        } else if (key == LV_KEY_DOWN) {
            if (selected_index + 1 < static_cast<int32_t>(item_count_)) {
                ++selected_index;
                scroll_to_selected(true);
            }
        } else if (key == LV_KEY_ENTER || key == LV_KEY_RIGHT) {
            activate_selected();
        }

        lv_event_stop_processing(event);
    }

    void create_ui(lv_obj_t *parent) override
    {
        if (!parent) return;

        ensure_async_dispatch();

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
        DComponens::lvgl_bind_event(ComponensObj, LV_EVENT_KEY, nullptr,
                                    std::bind(&LvSettingValuePage3Base::handle_key_event, this, std::placeholders::_1));

        selection_bg_ = lv_obj_create(ComponensObj);
        if (selection_bg_) {
            lv_obj_set_size(selection_bg_, metric(LayoutMetric::BarW), metric(LayoutMetric::BarH));
            lv_obj_set_pos(selection_bg_, metric(LayoutMetric::BarX), metric(LayoutMetric::BarY));
            lv_obj_set_style_bg_color(selection_bg_, lv_color_hex(0x2A2A2A), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(selection_bg_, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_border_width(selection_bg_, 0, LV_PART_MAIN);
            lv_obj_set_style_radius(selection_bg_, 0, LV_PART_MAIN);
            lv_obj_remove_flag(selection_bg_, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_remove_flag(selection_bg_, LV_OBJ_FLAG_SCROLLABLE);
        }

        value_list_ = lv_obj_create(ComponensObj);
        if (value_list_) {
            lv_obj_set_size(value_list_, metric(LayoutMetric::ValueListW), metric(LayoutMetric::ScreenH));
            lv_obj_set_pos(value_list_, metric(LayoutMetric::ValueListX), 0);
            lv_obj_set_style_bg_opa(value_list_, LV_OPA_TRANSP, LV_PART_MAIN);
            lv_obj_set_style_border_width(value_list_, 0, LV_PART_MAIN);
            lv_obj_set_style_pad_all(value_list_, 0, LV_PART_MAIN);
            lv_obj_set_style_pad_top(value_list_, metric(LayoutMetric::EdgePadding), LV_PART_MAIN);
            lv_obj_set_style_pad_bottom(value_list_, metric(LayoutMetric::EdgePadding), LV_PART_MAIN);
            lv_obj_set_style_pad_row(value_list_, 0, LV_PART_MAIN);
            lv_obj_set_style_radius(value_list_, 0, LV_PART_MAIN);
            lv_obj_set_style_clip_corner(value_list_, true, LV_PART_MAIN);
            lv_obj_set_flex_flow(value_list_, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_scroll_dir(value_list_, LV_DIR_VER);
            lv_obj_set_scroll_snap_y(value_list_, LV_SCROLL_SNAP_CENTER);
            lv_obj_set_scrollbar_mode(value_list_, LV_SCROLLBAR_MODE_OFF);
            lv_obj_remove_flag(value_list_, LV_OBJ_FLAG_SCROLL_WITH_ARROW);

            for (auto it = parent_node_.begin(); it != parent_node_.end(); ++it) {
                lv_obj_t *row = lv_obj_create(value_list_);
                if (!row) continue;
                value_rows_.push_back(row);
                lv_obj_set_size(row, metric(LayoutMetric::ValueListW), metric(LayoutMetric::RowH));
                lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
                lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
                lv_obj_set_style_radius(row, 0, LV_PART_MAIN);
                lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
                lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);
                lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

                lv_obj_t *label = lv_label_create(row);
                if (label) lv_label_set_text(label, it->label.c_str());
            }
            item_count_ = static_cast<uint32_t>(value_rows_.size());
        }

        title_label_ = lv_label_create(ComponensObj);
        if (title_label_) {
            std::string title = parent_node_->label;
            if (title.rfind("BQ ", 0) == 0) title.erase(0, 3);
            lv_label_set_text(title_label_, title.c_str());
            lv_obj_set_style_text_font(
                title_label_,
                cp0_fonts().get("Montserrat-Bold.ttf", 16, LV_FREETYPE_FONT_STYLE_BOLD),
                LV_PART_MAIN);
            lv_obj_set_style_text_color(title_label_, lv_color_white(), LV_PART_MAIN);
            lv_obj_set_style_text_align(title_label_, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
            update_title_position();
        }

        right_arrow_ = lv_img_create(ComponensObj);
        if (right_arrow_) {
            lv_img_set_src(right_arrow_, &setting_right_arrow);
            lv_image_set_pivot(right_arrow_, 0, 0);
            lv_image_set_scale(right_arrow_, metric(LayoutMetric::RightArrowScale));
            lv_obj_update_layout(right_arrow_);
            lv_obj_set_pos(
                right_arrow_, metric(LayoutMetric::ValueListX) - lv_obj_get_width(right_arrow_) - 4,
                metric(LayoutMetric::BarY) + (metric(LayoutMetric::BarH) - lv_obj_get_height(right_arrow_)) / 2);
        }

        arrow_up_ = lv_img_create(ComponensObj);
        if (arrow_up_) {
            lv_img_set_src(arrow_up_, &setting_red_up);
            lv_obj_update_layout(arrow_up_);
            lv_obj_set_pos(
                arrow_up_,
                metric(LayoutMetric::ValueListX) + metric(LayoutMetric::ValueCenterX) - lv_obj_get_width(arrow_up_) / 2,
                2);
        }

        arrow_down_ = lv_img_create(ComponensObj);
        if (arrow_down_) {
            lv_img_set_src(arrow_down_, &setting_red_down);
            lv_obj_update_layout(arrow_down_);
            lv_obj_set_pos(arrow_down_,
                           metric(LayoutMetric::ValueListX) + metric(LayoutMetric::ValueCenterX) -
                               lv_obj_get_width(arrow_down_) / 2,
                           metric(LayoutMetric::ScreenH) - lv_obj_get_height(arrow_down_) - 4);
        }

        hint_ = lv_label_create(ComponensObj);
        if (hint_) {
            lv_label_set_text(hint_, "ok:set");
            lv_obj_set_style_text_color(hint_, lv_color_hex(0x00CC66), LV_PART_MAIN);
            lv_obj_set_style_text_font(hint_, cp0_fonts().get("Montserrat-Bold.ttf", 16, LV_FREETYPE_FONT_STYLE_BOLD),
                                       LV_PART_MAIN);
            lv_obj_update_layout(hint_);
            lv_obj_set_pos(hint_, metric(LayoutMetric::ScreenW) - 6 - lv_obj_get_width(hint_),
                           metric(LayoutMetric::BarY) + (metric(LayoutMetric::BarH) - lv_obj_get_height(hint_)) / 2);
        }

        if (item_count_ > 0) {
            lv_obj_update_layout(value_list_);
            const int saved_selection = parent_node_->selected_index;
            select(saved_selection >= 0 ? saved_selection : initial_selection());
        } else
            update_arrow_visibility();
    }

private:
    NodeIter parent_node_;
    uint32_t item_count_ = 0;
    std::list<lv_obj_t *> value_rows_;
    std::weak_ptr<SettingRequestState> activation_state_;
    ActivationSink activation_sink_;
    uint64_t activation_generation_ = 0;
    int32_t activation_index_       = -1;
    bool activation_pending_        = false;
    lv_obj_t *selection_bg_ = nullptr;
    lv_obj_t *value_list_   = nullptr;
    lv_obj_t *title_label_  = nullptr;
    lv_obj_t *right_arrow_  = nullptr;
    lv_obj_t *arrow_up_     = nullptr;
    lv_obj_t *arrow_down_   = nullptr;
    lv_obj_t *hint_         = nullptr;
};

}  // namespace DComponens

using DComponens::LvSettingValuePage3Base;
