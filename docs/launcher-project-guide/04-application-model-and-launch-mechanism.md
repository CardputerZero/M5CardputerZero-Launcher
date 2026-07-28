# 04 - Application Model and Launch Mechanism

APPLaunch presents internal LVGL pages, terminal commands, standalone processes, and discovered `.desktop` files through one `std::list<app>`.

## 1. The `app` Model

`projects/APPLaunch/main/ui/launch.h` defines:

```cpp
struct app {
    std::string Name;
    std::string Icon;
    std::string Exec;
    std::function<void(Launch *)> launch;
};
```

Constructor overloads install the appropriate launch callback. The carousel only needs the label, icon, and callback; it does not branch on application type.

## 2. Three Launch Paths

| Type | Construction | Runtime behavior |
| --- | --- | --- |
| Internal page | `page_v<PageT>` | Creates `PageT`, sets `navigate_home`, calls `cp0_lvgl_start_app_page()` |
| Terminal command | command plus `terminal=true` | Creates `UISTPage`, starts it, then calls `exec(command)` |
| Standalone process | command plus `terminal=false` | Calls `Launch::launch_Exec()` and waits through `ExecBlocking` |

Internal and terminal pages remain in the APPLaunch process. Standalone applications temporarily take over the foreground and return control when the blocking process request completes.

## 3. Fixed Application Registry

Fixed entries are owned by `projects/APPLaunch/main/ui/builtin_app_registry.cpp`, not by `launch.cpp`. Each `BuiltinAppRegistration` contains an `AppDescriptor`, an optional command, process flags, and an optional typed page appender.

The current registry is:

| Label | Type | Target | Availability |
| --- | --- | --- | --- |
| Python | Terminal | `python3` | All builds |
| STORE | Standalone | `appstore_exec` platform path | All builds |
| CLI | Internal | `UISTPage` | All builds |
| GAME | Internal | `UIGamePage` | All builds |
| SETTING | Internal | `UISetupPage` | All builds |
| MATH | Standalone | `calculator_exec` platform path | Configurable |
| LORA | Internal | `UILoraPage` | Configurable |
| IP_PANEL | Internal | `UIIpPanelPage` | Linux device builds only |
| SSH | Internal | `UISSHPage` | Linux device builds only |
| TANK | Internal | `UITankBattlePage` | Linux device builds only |

`launcher_append_enabled_builtin_apps()` applies `launcher_app_registry_is_enabled()` and appends entries in table order. `Launch::rebuild_builtin_apps()` clears the list and invokes this function.

## 4. Dynamic `.desktop` Entries

`desktop_app_loader.cpp` scans the runtime applications directory, parses supported desktop fields, resolves icons and commands, and appends valid entries. It rejects commands already owned by the fixed registry so an installed desktop file does not duplicate STORE, MATH, or another command-backed fixed item.

`AppDirectoryWatcher` triggers `Launch::applications_reload()` when the directory changes. Reloading rebuilds fixed entries first and discovered entries second.

## 5. Selection and Carousel Mapping

`Launch::current_app` identifies the centered item. `normalized_app_index()` wraps indices across the list. `carousel_slot_app(slot)` maps the five visible slots around the center, and `UILaunchPage::refresh_carousel()` updates the view.

Movement and rendering are separated:

- `ui_launch_page_input.cpp` interprets key and click events.
- `ui_launch_page.cpp` updates navigation and animation state.
- `ui_launch_page_carousel_view.cpp` creates LVGL objects and changes their content.

## 6. Internal Page Lifecycle

The typed `app` constructor performs the launch sequence:

```text
begin_page_launch()
  -> show and flush Loading overlay
  -> construct PageT
  -> retain it in app_Page
  -> assign navigate_home
  -> hide Loading overlay
  -> cp0_lvgl_start_app_page(page)
```

`app_Page` keeps the page alive until the asynchronous home transition completes. `LauncherPageLifecycleModel` prevents double launch, duplicate home requests, and release during an active callback.

## 7. External Process Lifecycle

`Launch::launch_Exec()` performs a symmetric handoff:

```text
home active
  -> disable foreground screen saver behavior
  -> clear input group
  -> disable LVGL timers
  -> ExecBlocking
  -> restore timers and home input group
  -> show and refresh home
  -> restore foreground state
```

The root-retention flag is passed to the shared process service. ESC state is exchanged through the atomic `cp0_esc_state_*` C ABI rather than a shared variable address. Process execution details therefore belong to `cp0_lvgl`; APPLaunch owns only UI suspension and restoration.

## 8. Adding an Entry

For an internal page, add the class under `main/ui/page_app/`, ensure it is included by the generated page aggregate, then add a typed entry to `BUILTIN_APPS` in `builtin_app_registry.cpp`. For a command-backed entry, provide the command and flags with no page appender. Use `@name` for paths resolved by `launcher_platform::path()`.

Keep labels, icon filenames, configuration keys, and Settings visibility in the `AppDescriptor`; do not duplicate registry metadata in the carousel.
