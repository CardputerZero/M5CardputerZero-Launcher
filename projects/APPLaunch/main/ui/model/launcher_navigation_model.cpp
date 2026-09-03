/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "launcher_navigation_model.hpp"

bool LauncherStartupDelayModel::begin()
{
    if (state_ != LauncherStartupDelayState::IDLE)
        return false;
    state_ = LauncherStartupDelayState::WAITING;
    return true;
}

bool LauncherStartupDelayModel::timer_created(bool created)
{
    if (state_ != LauncherStartupDelayState::WAITING || created)
        return false;
    state_ = LauncherStartupDelayState::COMPLETE;
    return true;
}

bool LauncherStartupDelayModel::complete()
{
    if (state_ != LauncherStartupDelayState::WAITING)
        return false;
    state_ = LauncherStartupDelayState::COMPLETE;
    return true;
}

void LauncherStartupDelayModel::stop()
{
    state_ = LauncherStartupDelayState::STOPPED;
}

bool LauncherPageLifecycleModel::begin_app()
{
    if (state_ != LauncherPageState::HOME) return false;
    state_ = LauncherPageState::APP_ACTIVE;
    return true;
}

bool LauncherPageLifecycleModel::abort_app()
{
    if (state_ != LauncherPageState::APP_ACTIVE) return false;
    state_ = LauncherPageState::HOME;
    return true;
}

bool LauncherPageLifecycleModel::request_home()
{
    if (state_ != LauncherPageState::APP_ACTIVE) return false;
    state_ = LauncherPageState::HOME_PENDING;
    return true;
}

bool LauncherPageLifecycleModel::complete_home()
{
    if (state_ != LauncherPageState::HOME_PENDING) return false;
    state_ = LauncherPageState::HOME;
    return true;
}

void LauncherPageLifecycleModel::cancel_home_request()
{
    if (state_ == LauncherPageState::HOME_PENDING)
        state_ = LauncherPageState::APP_ACTIVE;
}

void LauncherPageLifecycleModel::stop()
{
    state_ = LauncherPageState::STOPPED;
}

LauncherPageState LauncherPageLifecycleModel::state() const
{
    return state_;
}

bool LauncherNavigationModel::begin_navigation(LauncherNavigationDirection direction)
{
    if (is_animating_)
        return false;

    previous_page_ = selected_page_;
    is_animating_ = true;
    if (direction == LauncherNavigationDirection::PREVIOUS)
        selected_page_ = selected_page_ == 0 ? PAGE_COUNT - 1 : selected_page_ - 1;
    else
        selected_page_ = (selected_page_ + 1) % PAGE_COUNT;
    return true;
}

void LauncherNavigationModel::finish_navigation()
{
    is_animating_ = false;
    previous_page_ = selected_page_;
}

bool LauncherNavigationModel::cancel_navigation()
{
    if (!is_animating_)
        return false;
    selected_page_ = previous_page_;
    is_animating_ = false;
    return true;
}

size_t LauncherNavigationModel::selected_page() const
{
    return selected_page_;
}

bool LauncherNavigationModel::is_animating() const
{
    return is_animating_;
}

bool LauncherNavigationModel::keyboard_navigation_enabled() const
{
    return !is_animating_;
}
