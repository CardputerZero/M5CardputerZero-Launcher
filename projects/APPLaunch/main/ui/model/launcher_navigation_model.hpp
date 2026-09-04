/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>

enum class LauncherNavigationDirection
{
    PREVIOUS,
    NEXT,
};

enum class LauncherPageState
{
    HOME,
    APP_ACTIVE,
    HOME_PENDING,
    STOPPED,
};

class LauncherPageLifecycleModel
{
public:
    bool begin_app();
    bool abort_app();
    bool request_home();
    bool complete_home();
    void cancel_home_request();
    void stop();

    LauncherPageState state() const;

private:
    LauncherPageState state_ = LauncherPageState::HOME;
};

class LauncherNavigationModel
{
public:
    static constexpr size_t PAGE_COUNT = 5;
    static constexpr size_t INITIAL_PAGE = PAGE_COUNT / 2;

    bool begin_navigation(LauncherNavigationDirection direction);
    void finish_navigation();
    bool cancel_navigation();

    size_t selected_page() const;
    bool is_animating() const;
    bool keyboard_navigation_enabled() const;

private:
    size_t selected_page_ = INITIAL_PAGE;
    size_t previous_page_ = INITIAL_PAGE;
    bool is_animating_ = false;
};

enum class LauncherStartupDelayState
{
    IDLE,
    WAITING,
    COMPLETE,
    STOPPED,
};

class LauncherStartupDelayModel
{
public:
    bool begin();
    bool timer_created(bool created);
    bool complete();
    void stop();
    LauncherStartupDelayState state() const { return state_; }

private:
    LauncherStartupDelayState state_ = LauncherStartupDelayState::IDLE;
};
