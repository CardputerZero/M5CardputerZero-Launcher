# 02 - Runtime Framework and Boot Flow

APPLaunch uses the shared `cp0_lvgl` application runner. The process entry point supplies launcher-specific setup and teardown callbacks; it does not duplicate LVGL initialization, the event loop, signal handling, or shutdown code.

## 1. Main Entry Point

`projects/APPLaunch/main/src/main.cpp` builds `Cp0LvglRunOptions` and calls:

```cpp
return cp0_lvgl_run(std::move(options));
```

The setup callback performs only launcher work:

1. Registers `LV_EVENT_KEYBOARD` when necessary.
2. Calls `launcher_ui::init()`.
3. Initializes the screen saver.

The teardown callback calls `launcher_ui::deinit()`. Shared initialization and the LVGL run loop belong to `cp0_lvgl_app_runner.hpp` in the `cp0_lvgl` component.

## 2. Launcher UI Ownership

`launcher_ui::init()` creates `LauncherUiRuntime`. The runtime owns a `Launch` application manager and a `UILaunchPage` home page, connects them, creates the home screen and input group, and then loads either the startup animation or the home screen.

```text
main.cpp
  -> cp0_lvgl_run(options)
     -> options.setup()
        -> launcher_ui::init()
           -> LauncherUiRuntime::init()
              -> Launch
              -> UILaunchPage
              -> create_screen() / init_input_group()
              -> Launch::bind_ui()
              -> startup animation or home screen
```

The relevant implementation is split across:

| File | Responsibility |
| --- | --- |
| `main/src/main.cpp` | Configure and enter the shared runner |
| `main/ui/ui.cpp` | Public launcher UI initialization and global UI state |
| `main/ui/launcher_ui_runtime.cpp` | Own and connect the manager and home page |
| `main/ui/ui_launch_page_carousel_view.cpp` | Create and update carousel objects |
| `main/ui/ui_launch_page_input.cpp` | Home keyboard and click handling |
| `main/ui/ui_launch_page_startup.cpp` | Startup GIF, sound, and delayed home transition |
| `main/ui/ui_launch_page.cpp` | Shared home-page lifecycle and carousel state transitions |

## 3. Loading Applications

`Launch::bind_ui()` installs registry callbacks, rebuilds enabled fixed entries, loads `.desktop` applications, refreshes the carousel, starts the application-directory watcher, and registers the long-ESC callback.

Fixed entries come from `builtin_app_registry.cpp`. Dynamic entries come from `desktop_app_loader.cpp`. A directory change invokes `Launch::applications_reload()`, which reconstructs both sets and refreshes the five visible carousel slots.

## 4. Page and Process Switching

Internal pages are created as `std::shared_ptr<PageT>` and displayed with `cp0_lvgl_start_app_page()`. `LauncherPageLifecycleModel` rejects overlapping launches and home requests. Returning home is scheduled with `lv_async_call()` so the active event callback can finish before the page object is released.

External applications use `cp0_signal_process_api({"ExecBlocking", ...})`. Before the blocking call, APPLaunch disables the screen saver foreground state, unbinds input, disables LVGL timers, and refreshes the display. After the child exits it restores timers, the home input group, the home screen, and foreground state.

This coordination is implemented inside `Launch::launch_Exec()`; there is no separate polling function in the launcher main loop.

## 5. Returning Home

Built-in and terminal pages receive a `navigate_home` callback bound to `Launch::go_back_home()`. The callback requests a lifecycle transition and schedules `Launch::lv_go_back_home()`.

The asynchronous callback:

1. Re-enables LVGL timers.
2. Calls `UILaunchPage::show_home_screen()`.
3. Forces a refresh.
4. Releases the active page holder.

Holding ESC for the configured interval calls `Launch::esc_force_home_cb()` for an active internal page. External process cancellation reads the atomic ESC state through the `cp0_esc_state_*` C ABI; `ExecBlocking` does not receive a shared-memory address.

## 6. Debugging Checkpoints

- If the process never reaches the UI, inspect the shared runner result and the `[BOOT]` message from the setup callback.
- If home is blank, verify `LauncherUiRuntime::init()`, `UILaunchPage::create_screen()`, and `Launch::bind_ui()`.
- If a page cannot return, inspect `LauncherPageLifecycleModel`, `navigate_home`, and pending `lv_async_call()` work.
- If an external app leaves input or timers disabled, inspect both sides of `Launch::launch_Exec()` and the `ExecBlocking` response.
