# 06 - Resources and Configuration System

This chapter explains APPLaunch runtime resource directories, path resolution rules, `.desktop` dynamic application files, configuration APIs, settings-page configuration keys, and resource usage notes. Key source files are `ext_components/cp0_lvgl/include/cp0_lvgl_app.h`, `ext_components/cp0_lvgl/src/cp0/cp0_lvgl_file.cpp`, `ext_components/cp0_lvgl/src/sdl/sdl_lvgl_file.cpp`, `projects/APPLaunch/main/ui/launch.cpp`, and `projects/APPLaunch/main/ui/page_app/ui_app_setup.hpp`.

## 1. Resource System Overview

APPLaunch pages should not manually concatenate runtime paths. Instead, use `cp0_file_path()` / `cp0_file_path_c()` for unified resolution.

```text
Page code / launch.cpp
        |
        v
img_path(), audio_path(), cp0_file_path_c()
        |
        v
cp0_lvgl_file.cpp / sdl_lvgl_file.cpp
        |
        +-- Images: share/images/...
        +-- Audio: /usr/share/APPLaunch/share/audio/...
        +-- Fonts: /usr/share/APPLaunch/share/font/...
        +-- applications: /usr/share/APPLaunch/applications
        +-- Special paths such as keyboard_device / keyboard_map / lock_file
```

Common wrapper functions for pages are located in `projects/APPLaunch/main/ui/ui_app_page.hpp`:

```cpp
static inline std::string img_path(const char *name)
{
    return cp0_file_path(name);
}

static inline std::string audio_path(const char *name)
{
    return cp0_file_path(name);
}
```

## 2. Runtime Resource Tree

The source resource tree is located at:

```text
projects/APPLaunch/APPLaunch/
├── applications/
├── bin/
├── lib/
└── share/
    ├── audio/
    ├── font/
    └── images/
```

After installation on a device, it usually maps to:

```text
/usr/share/APPLaunch/
├── applications/
├── bin/
├── lib/
└── share/
    ├── audio/
    ├── font/
    └── images/
```

| Directory | Contents | Used by |
| --- | --- | --- |
| `applications/` | `.desktop` application descriptors | `Launch::applications_load()` |
| `share/images/` | Icons, status-bar backgrounds, page images, GIFs | Home screen, top bar, built-in pages |
| `share/audio/` | `startup.mp3`, `switch.wav`, `enter.wav`, page key sounds | Home sound effects, settings page, page sound effects |
| `share/font/` | TTF/OTF fonts | `LauncherFonts`, page custom fonts |
| `bin/` | Packaged scripts and external programs | Store, update scripts, dynamic applications |
| `lib/` | Packaged dynamic libraries | External programs or platform capabilities |

## 3. Path Resolution Rules

### 3.1 Device-Side `cp0_lvgl_file.cpp`

The device-side implementation is in `ext_components/cp0_lvgl/src/cp0/cp0_lvgl_file.cpp`, with a fixed root directory:

```cpp
constexpr const char *kAppRoot = "/usr/share/APPLaunch";
```

Core rules:

| Input | Output |
| --- | --- |
| `applications` | `/usr/share/APPLaunch/applications` |
| `lock_file` | `/tmp/M5CardputerZero-APPLaunch_fcntl.lock` |
| `keyboard_device` | `/dev/input/by-path/platform-3f804000.i2c-event` |
| `keyboard_map` | `/usr/share/keymaps/tca8418_keypad_m5stack_keymap.map` |
| `*.png` / `*.gif` / `*.jpg` / `*.jpeg` / `*.svg` | `share/images/<file>` |
| `*.wav` / `*.mp3` / `*.ogg` | `/usr/share/APPLaunch/share/audio/<file>` |
| `*.ttf` / `*.otf` | `/usr/share/APPLaunch/share/font/<file>` |
| Other strings | Returned unchanged |

The current device-side image rule returns a relative path such as `share/images/<file>`, while audio and fonts return absolute paths under `/usr/share/APPLaunch/...`. Page code should follow the existing `img_path("xxx.png")` convention and not mix multiple root directories.

### 3.2 SDL Implementation `sdl_lvgl_file.cpp`

The SDL implementation is in `ext_components/cp0_lvgl/src/sdl/sdl_lvgl_file.cpp`. Its rules are mostly the same as the device-side implementation, but the root is decided by `get_app_root_path()`, and functions such as `app_relative_path(root_path, file, "share/images/")` adapt to development-machine run directories.

Special names are still kept under SDL:

```cpp
if (file == "applications") return root_path + "/applications";
if (file == "keyboard_device") return "/dev/input/by-path/platform-3f804000.i2c-event";
if (file == "keyboard_map") return "/usr/share/keymaps/tca8418_keypad_m5stack_keymap.map";
```

Note: SDL mode only makes APPLaunch runnable on a development machine. It does not mean every device resource exists. For example, camera, LoRa, and some Linux device pages may be excluded by compile-time conditions or unavailable at runtime.

### 3.3 `cp0_file_path_c()` Cache

The C interface is declared in `ext_components/cp0_lvgl/include/cp0_lvgl_app.h`:

```c
const char *cp0_file_path_c(const char *file);
```

The implementation uses a `thread_local std::unordered_map<std::string, std::string>` cache:

```cpp
extern "C" const char *cp0_file_path_c(const char *file)
{
    static thread_local std::unordered_map<std::string, std::string> paths;
    std::string key = file ? std::string(file) : std::string();
    auto it = paths.find(key);
    if (it == paths.end()) {
        it = paths.emplace(key, cp0_file_path(key)).first;
    }
    return it->second.c_str();
}
```

Therefore, the returned `const char *` is stable within the thread and can be passed directly to LVGL style or image APIs. If saving the pointer across threads, prefer saving a `std::string` instead.

## 4. Image, Audio, and Font Usage Examples

### 4.1 Images

Common home-screen and built-in-page usage:

```cpp
{{"GAME", "game_100.png", "app_Game", false, true},
 nullptr, false, true, false, append_page_app<UIGamePage>},

lv_obj_set_style_bg_img_src(time_panel_,
    cp0_file_path_c("status_time_background.png"),
    LV_PART_MAIN | LV_STATE_DEFAULT);
```

Home card icons are set by `launch.cpp::panel_set_icon()`:

```cpp
static void panel_set_icon(lv_obj_t *panel, const char *src)
{
    lv_obj_t *img = lv_obj_get_child(panel, 0);
    if (!img || !lv_obj_check_type(img, &lv_image_class)) {
        img = lv_image_create(panel);
        lv_obj_set_size(img, LV_PCT(100), LV_PCT(100));
        lv_image_set_inner_align(img, LV_IMAGE_ALIGN_STRETCH);
    }
    lv_image_set_src(img, src);
}
```

Note: `panel_set_icon()` checks `access(icon_src, R_OK)` and writes a log. If the image path on the device side is relative, the runtime working directory must be correct; otherwise the log will report the image as missing/unreadable.

### 4.2 Audio

Home sound effects play asset names through system audio signals:

```cpp
static void audio_play_ui_asset(const char *name)
{
    cp0_signal_system_play_asset(name);
}

static void audio_play_switch(void) { audio_play_ui_asset("switch.wav"); }
static void audio_play_enter(void)  { audio_play_ui_asset("enter.wav"); }
```

The startup sound is played after the home screen loads:

```cpp
cp0_signal_audio_api_play_asset("startup.mp3");
```

Inside pages, use `audio_path("key_enter.wav")` if a file path is needed. If the lower-level API expects an asset name rather than a path, pass the asset name directly to avoid resolving the path twice.

### 4.3 Fonts

The home screen and top bar use `LauncherFonts` to manage freetype fonts:

```cpp
launcher_fonts().get("Montserrat-Bold.ttf", 16, LV_FREETYPE_FONT_STYLE_BOLD)
```

Font paths are ultimately resolved by `cp0_file_path()` into `share/font/`. If freetype font loading fails, `LauncherFonts` falls back to LVGL's built-in Montserrat font.

## 5. `.desktop` Dynamic Applications

Dynamic application files are placed in the directory pointed to by `cp0_file_path("applications")`. `Launch::applications_load()` only processes `*.desktop` files and parses the `[Desktop Entry]` section.

Supported keys:

| key | Required | Description |
| --- | --- | --- |
| `Name` | Yes | Carousel display name |
| `Exec` | Yes | Launch command or executable path |
| `Icon` | No | Icon path; can be `share/images/...` or a path readable by LVGL |
| `Terminal` | No | `true`/`True`/`1` means run inside `UISTPage` |
| `Sysplause` | No | Whether to pause and wait for user confirmation after a terminal command exits; defaults to `true` |

Example:

```ini
[Desktop Entry]
Name=Vim
TryExec=vim
Exec=vim
Terminal=true
Icon=share/images/e-Mail_80.png
Sysplause=true
```

Loading rules:

- Only key-value pairs inside the `[Desktop Entry]` section are read.
- Blank lines and comments starting with `#` or `;` are skipped.
- Entries missing either `Name` or `Exec` are skipped.
- If `Exec` duplicates an existing app, the entry is skipped.
- `TryExec` is currently not used by `applications_load()`.
- The `applications/` directory is watched. It is polled every 3 seconds, and changes clear dynamic apps and rescan the directory.

Dynamic desktop applications also carry a runtime origin. Product packages
are recognized by a trusted desktop filename and a label in the product's
built-in app set, even when their `.desktop` file is delivered by a separate
app repository. The manifest at
`/usr/share/APPLaunch/preinstalled-desktop-apps.tsv` remains available for
release-owned entries such as ZClaw whose package identity is not part of the
static app set. Other desktop entries are treated as Store-installed or
user-installed. The Settings `Launcher` menu includes built-in and
release-preinstalled configurable applications, but does not expose
Store-installed entries or the protected Settings, Store, and CLI applications.
All dynamic entries remain visible on the home launcher; only product-owned
entries receive a Settings toggle.

## 6. Configuration API and Persistence Paths

The current configuration service is called through `cp0_signal_config_api`. It supports the `Init`, `Save`, `GetInt`, `SetInt`, `GetStr`, and `SetStr` commands. The service is implemented in `ext_components/cp0_lvgl/src/cp0_config_service.cpp`; the device and SDL backends register the signal in `ext_components/cp0_lvgl/src/cp0/cp0_lvgl_config.cpp` and `ext_components/cp0_lvgl/src/sdl/sdl_lvgl_config.cpp`.

Typical read:

```cpp
int value = default_value;
cp0_signal_config_api(
    {"GetInt", key, std::to_string(default_value)},
    [&](int code, std::string data) {
        if (code == 0) value = std::stoi(data);
    });
```

A write must call `SetInt` / `SetStr` and then `Save`. The current settings page and application registry restore the previous value when saving fails:

```cpp
cp0_signal_config_api({"SetInt", key, std::to_string(value)}, set_callback);
cp0_signal_config_api({"Save"}, save_callback);
```

Always provide a default when reading. `SetInt` / `SetStr` only update the in-memory entries; persistence is complete only after `Save` succeeds.

| Runtime | Path selection |
| --- | --- |
| Device | `$HOME/.config/cardputerzero/config.json`; falls back to `/root/.config/cardputerzero/config.json` when `HOME` is empty |
| SDL | `$APPLAUNCH_SDL_CONFIG_DIR/config.json`, then `$XDG_CONFIG_HOME/applaunch-sdl/config.json`, then `$HOME/.config/applaunch-sdl/config.json`; if none are available, `sdl_config/config.json` under the runtime data directory |

Both backends write a temporary file, call `fsync()`, and rename it over the destination.

## 7. Configuration Key List

### 7.1 Launcher Application Toggles

The `Launcher` menu in `UISetupPage` saves `app_<Name>`:

| Configuration key | Default | Meaning | Notes |
| --- | --- | --- | --- |
| `app_Python` | `1` | Python entry | always-on, cannot be disabled |
| `app_Store` | `1` | Store entry | always-on, cannot be disabled |
| `app_CLI` | `1` | CLI entry | always-on, cannot be disabled |
| `app_Game` | `1` | GAME entry | always-on, cannot be disabled |
| `app_Setting` | `1` | SETTING entry | always-on, cannot be disabled |
| `app_Math` | `1` | Calculator external application | Configurable |
| `app_LoRa` | `1` | LORA built-in page | Configurable and registered for every build target |
| `app_IP_Panel` | `1` | IP_PANEL built-in page | Configurable; registered only in Linux non-SDL builds |
| `app_SSH` | `1` | SSH built-in page | Configurable; registered only in Linux non-SDL builds |
| `app_Tank` | `1` | TANK built-in page | Configurable; registered only in Linux non-SDL builds |

These entries come from `BUILTIN_APPS[]` in `projects/APPLaunch/main/ui/builtin_app_registry.cpp`. Release-preinstalled desktop entries use a stable `app_desktop_*` key derived from the desktop filename. Entries with `configurable=false` or `always_on=true` are always enabled; other entries default to enabled when their key is absent.

### 7.2 System and Page Configuration

| Configuration key | Read/write location | Meaning |
| --- | --- | --- |
| `brightness` | `UISetupPage`, `ext_components/cp0_lvgl/src/commount.cpp` | Backlight brightness value; restored at startup and written by the settings page |
| `volume` | `UISetupPage`, `commount.cpp` | System volume; restored at startup and written by the settings page |
| `dark_time` | `UISetupPage` | Screen-off timeout, options are `0/10/30/60/300` seconds |
| `bt_named_only` | Bluetooth settings | Whether to show only named Bluetooth devices; defaults to `1` |
| `run_as_user` | `cp0_process_commands.cpp`, `cp0_sudo_async.cpp` | User configuration for dropping privileges in external processes / PTY commands |

`extport_usb` and `extport_5vout` are GPIO names passed to `cp0_signal_settings_api`, not configuration-file keys.

### 7.3 Temporary Business Inputs

The following are mostly in-memory page state and are not persisted by default:

- Host/Port/User defaults in `UISSHPage` are initialized in the constructor and are not written to configuration.
- The `UIMeshPage` message input buffer only exists in page memory.
- The network interface list in `UIIpPanelPage` is refreshed every second from `cp0_network_list()`.

## 8. Settings-Page Configuration Write Paths

`UISetupPage` is where configuration keys are concentrated. Typical functions:

- `menu_init()`: builds the settings menu through the settings controllers.
- `launcher_app_registry_set_enabled()`: saves configurable `app_*` toggles and restores the previous value when saving fails.
- `Screen::apply_value()`: applies and saves brightness and screen-off timeout.
- `Speaker::apply_value()`: writes system volume and saves `volume`.
- `Bluetooth::toggle_named_only()`: saves `bt_named_only`.

Example:

```cpp
bool launcher_app_registry_set_enabled(const AppDescriptor &desc, bool enabled)
{
    if (desc.always_on || !desc.configurable) return true;
    // SetInt, Save, and restore the old value if either step fails.
}
```

When changing configuration keys, check all of the following in sync:

- `AppDescriptor` entries in `BUILTIN_APPS[]`.
- The Launcher settings controller that uses `launcher_app_registry_entries()`.
- `launcher_app_registry_is_enabled()` / `launcher_app_registry_set_enabled()` in `app_registry.cpp`.
- Documentation and default configuration.

## 9. Resource Naming Recommendations

- Name home icons as `<app>_100.png`, such as `game_100.png` and `setting_100.png`.
- Name small icons or status backgrounds by function, such as `status_time_background.png` and `status_battery_background.png`.
- Use a page prefix for page-specific resources, such as `setting_ok.png` and `setting_cross.png`.
- Use short names for sound effects, such as `switch.wav`, `enter.wav`, and `key_back.wav`.
- Use real file names for fonts, such as `Montserrat-Bold.ttf`, and load them through `launcher_fonts().get()`.

## 10. Common Issues and Notes

- Image and audio extensions are lowercased for classification, but the file system is still case-sensitive, so the file name itself must match.
- `cp0_file_path()` classifies only by extension and does not check whether the file exists.
- The `.desktop` `Icon` value does not automatically call `cp0_file_path()`; use a path that LVGL can read directly, or keep it consistent with existing templates.
- If a new resource is used on the device side, confirm that packaging scripts include `projects/APPLaunch/APPLaunch/share/...` in the install package.
- If `Save` does not succeed after `SetInt` / `SetStr`, the value will be lost after reboot.
- Changing an `app_*` toggle notifies Launcher to rebuild the application list; always-on entries are unaffected by toggle writes.
- `run_as_user` affects the execution identity of external processes and PTY commands. Check this setting when debugging permission issues.
