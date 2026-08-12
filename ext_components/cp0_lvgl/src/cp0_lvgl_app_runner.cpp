#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "cp0_lvgl_app_runner.hpp"

#include "cp0_runner_contract.hpp"
#include "cp0_runner_shutdown.hpp"
#include "hal_lvgl_bsp.h"

#include <chrono>
#include <cerrno>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#if !defined(__APPLE__)
#include <semaphore.h>
#endif
#include <time.h>

namespace {

#if defined(__APPLE__)
std::condition_variable lvgl_wake;
size_t pending_wakes = 0;
#else
sem_t lvgl_sem;
#endif
std::mutex runner_mutex;
cp0::RunnerLifetime runner_lifetime;
bool semaphore_ready = false;

void post_lvgl_semaphore()
{
    std::lock_guard<std::mutex> lock(runner_mutex);
    if (!semaphore_ready) return;
#if defined(__APPLE__)
    ++pending_wakes;
    lvgl_wake.notify_one();
#else
    sem_post(&lvgl_sem);
#endif
}

void resume_cb(void *)
{
    cp0::invoke_resume_callback([] { post_lvgl_semaphore(); });
}

void wait_for_lvgl(uint32_t milliseconds)
{
#if defined(__APPLE__)
    std::unique_lock<std::mutex> lock(runner_mutex);
    lvgl_wake.wait_for(lock, std::chrono::milliseconds(milliseconds), [] {
        return pending_wakes != 0 || !semaphore_ready;
    });
    if (pending_wakes != 0) --pending_wakes;
#else
    struct timespec deadline;
#if defined(__linux__)
    clock_gettime(CLOCK_MONOTONIC, &deadline);
#else
    clock_gettime(CLOCK_REALTIME, &deadline);
#endif
    deadline.tv_nsec += (milliseconds % 1000) * 1000000;
    deadline.tv_sec += milliseconds / 1000 + deadline.tv_nsec / 1000000000;
    deadline.tv_nsec %= 1000000000;

    int result;
    do {
#if defined(__linux__)
        result = sem_clockwait(&lvgl_sem, CLOCK_MONOTONIC, &deadline);
#else
        result = sem_timedwait(&lvgl_sem, &deadline);
#endif
    } while (result != 0 && errno == EINTR);
#endif
}

void wait_for_lvgl()
{
#if defined(__APPLE__)
    std::unique_lock<std::mutex> lock(runner_mutex);
    lvgl_wake.wait(lock, [] {
        return pending_wakes != 0 || !semaphore_ready;
    });
    if (pending_wakes != 0) --pending_wakes;
#else
    int result;
    do {
        result = sem_wait(&lvgl_sem);
    } while (result != 0 && errno == EINTR);
#endif
}

void stop_audio() noexcept
{
#ifdef CONFIG_CP0_LVGL_INIT_AUDIO
    deinit_audio();
#endif
}

void stop_pty() noexcept
{
#ifdef CONFIG_CP0_LVGL_INIT_PTY
    deinit_pty();
#endif
}

void stop_camera() noexcept
{
#ifdef CONFIG_CP0_LVGL_INIT_CAMERA
    deinit_camera();
#endif
}

void stop_sudo() noexcept
{
#ifdef CONFIG_CP0_LVGL_INIT_SUDO
    deinit_sudo();
#endif
}

void stop_rpc() noexcept
{
#if defined(CONFIG_CP0_LVGL_INIT_RPC) && !defined(HAL_PLATFORM_SDL)
    deinit_rpc();
#endif
}

void stop_lora() noexcept
{
#ifdef CONFIG_CP0_LVGL_INIT_LORA
    deinit_lora();
#endif
}

} // namespace

void cp0_lvgl_wake()
{
    post_lvgl_semaphore();
}

int cp0_lvgl_run(Cp0LvglRunOptions options)
{
    {
        std::lock_guard<std::mutex> lock(runner_mutex);
        if (!runner_lifetime.claim()) {
            std::fprintf(stderr, "cp0_lvgl: runner supports one run per process\n");
            return 1;
        }
#if defined(__APPLE__)
        pending_wakes = 0;
#else
        if (sem_init(&lvgl_sem, 0, 0) != 0) {
            runner_lifetime.initialization_failed();
            return 1;
        }
#endif
        semaphore_ready = true;
    }

    lv_init();
    lv_timer_handler_set_resume_cb(resume_cb, nullptr);
    bool cleaned_up = false;
    auto cleanup = [&](bool teardown) {
        if (cleaned_up) return false;
        cleaned_up = true;

        bool teardown_failed = false;
        if (teardown && options.teardown) {
            try {
                options.teardown();
            } catch (...) {
                std::fprintf(stderr, "cp0_lvgl: teardown threw an exception\n");
                teardown_failed = true;
            }
        }
        lv_timer_handler_set_resume_cb(nullptr, nullptr);
        cp0::runner::shutdown_services(
            stop_sudo, stop_rpc, stop_camera, stop_audio, stop_pty,
            deinit_input, deinit_wifi, stop_lora, deinit_battery, lv_deinit);
        std::lock_guard<std::mutex> lock(runner_mutex);
        semaphore_ready = false;
#if defined(__APPLE__)
        pending_wakes = 0;
        lvgl_wake.notify_all();
#else
        sem_destroy(&lvgl_sem);
#endif
        runner_lifetime.finish();
        return teardown_failed;
    };

    bool setup_attempted = false;
    try {
        if (options.after_lvgl_init)
            options.after_lvgl_init();

        cp0_lvgl_init();
        if (options.after_resource_init)
            options.after_resource_init();

        if (!lv_display_get_default()) {
            std::fprintf(stderr, "cp0_lvgl: failed to create LVGL display\n");
            cleanup(false);
            return 1;
        }

        if (options.setup) {
            setup_attempted = true;
            if (!options.setup()) {
                cleanup(true);
                return 1;
            }
        }

        lv_obj_invalidate(lv_screen_active());
        lv_refr_now(nullptr);

        while (!options.should_quit || !options.should_quit()) {
            uint32_t milliseconds = lv_timer_handler();
            if (options.should_quit && options.should_quit())
                break;
            if (milliseconds == LV_NO_TIMER_READY)
                wait_for_lvgl();
            else
                wait_for_lvgl(milliseconds);
        }

        return cleanup(true) ? 1 : 0;
    } catch (...) {
        std::fprintf(stderr, "cp0_lvgl: runner callback threw an exception\n");
        cleanup(setup_attempted);
        return 1;
    }
}
