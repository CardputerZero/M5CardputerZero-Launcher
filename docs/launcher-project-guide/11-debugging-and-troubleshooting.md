# 11 - Debugging and Troubleshooting

This chapter covers common issues during APPLaunch development and device deployment. In general, reproduce UI, asset, and input logic issues with the SDL2 build first, then use device logs to locate framebuffer, evdev, permission, and systemd problems.

## 1. Common Debugging Commands

### 1.1 Check Repository and Build Status

```bash
cd /home/nihao/w2T/github/launcher

git status --short
find docs/launcher工程详细说明 -maxdepth 1 -type f | sort
find projects/APPLaunch/APPLaunch -maxdepth 3 -type f | sort | sed -n '1,160p'
```

### 1.2 Build and Run SDL2 Locally

```bash
cd /home/nihao/w2T/github/launcher/projects/APPLaunch
CONFIG_DEFAULT_FILE=linux_x86_sdl2_config_defaults.mk scons -j8 --implicit-deps-changed
./dist/M5CardputerZero-APPLaunch
```

Use this to:

- Quickly verify the home page, built-in pages, carousel animation, and `.desktop` scanning.
- Check LVGL object creation and asset paths.
- Avoid device-side framebuffer, evdev, and systemd permission issues.

### 1.3 Device-side / Cross Build

```bash
cd /home/nihao/w2T/github/launcher/projects/APPLaunch
CONFIG_DEFAULT_FILE=linux_x86_cross_cp0_config_defaults.mk scons -j8 --implicit-deps-changed
```

If building natively on the device:

```bash
cd /path/to/launcher/projects/APPLaunch
CONFIG_DEFAULT_FILE=config_defaults.mk scons -j4 --implicit-deps-changed
```

### 1.4 View APPLaunch Runtime Logs

If started by systemd:

```bash
systemctl --user status APPLaunch.service --no-pager
journalctl --user -u APPLaunch.service -b --no-pager
journalctl --user -u APPLaunch.service -f
```

If running the device binary manually:

```bash
systemctl --user stop APPLaunch.service
cd /usr/share/APPLaunch
sudo /usr/share/APPLaunch/bin/M5CardputerZero-APPLaunch 2>&1 | tee /tmp/applaunch.log
```

The actual binary path depends on packaging and installation. It may also be the build output `projects/APPLaunch/dist/M5CardputerZero-APPLaunch`.

### 1.5 Check Runtime Assets

```bash
ls -l /usr/share/APPLaunch
find /usr/share/APPLaunch/share/images -maxdepth 1 -type f | sort | sed -n '1,120p'
find /usr/share/APPLaunch/share/audio -maxdepth 1 -type f | sort
find /usr/share/APPLaunch/share/font -maxdepth 1 -type f | sort
find /usr/share/APPLaunch/applications -maxdepth 1 -type f | sort
```

### 1.6 Check Input Devices

```bash
ls -l /dev/input/by-path/
ls -l /dev/input/event*
sudo evtest
```

Default keyboard device in code:

```text
/dev/input/by-path/platform-3f804000.i2c-event
```

Override it with an environment variable:

```bash
APPLAUNCH_LINUX_KEYBOARD_DEVICE=/dev/input/eventX sudo /usr/share/APPLaunch/bin/M5CardputerZero-APPLaunch
```

### 1.7 Check Configuration Files

```bash
sudo ls -l /var/lib/applaunch
sudo cat /var/lib/applaunch/settings
```

Common configuration keys:

- `app_Game`, `app_Math`, `app_File`, `app_Camera`, etc.: Launcher page visibility toggles.
- `brightness`: brightness.
- `volume`: volume.
- `dark_time`: screen-off timeout.
- `cam_resolution`: camera resolution.
- `startup_mode`: startup mode.
- `extport_usb`, `extport_5vout`: extension port settings.
- `run_as_user`: user used when lowering privileges for external processes.

## 2. Log Keyword Quick Reference

| Keyword | Location | Meaning |
| --- | --- | --- |
| `[BOOT] lv_init() done` | `main.cpp` | LVGL initialization completed |
| `[BOOT] cp0_lvgl_init() starting...` | `main.cpp` | Starting platform adaptation layer, display, input, audio, and other initialization |
| `[BOOT] First frame flushed to fb0.` | `main.cpp` | First frame was forcibly flushed to the display device |
| `Entering main loop` | `main.cpp` | Main loop has started |
| `[LAUNCHER] set panel icon` | `launch.cpp` | Home icon was set successfully |
| `set panel icon missing/unreadable` | `launch.cpp` | Icon path does not exist or is unreadable |
| `applications_load: opendir failed` | `launch.cpp` | applications directory does not exist or is unreadable |
| `missing Name or Exec` | `launch.cpp` | `.desktop` is missing required fields |
| `duplicate Exec` | `launch.cpp` | `.desktop` has the same Exec as an existing app |
| `Launching terminal app` | `launch.cpp` | Entering the built-in terminal page to run a command |
| `Launching external app` | `launch.cpp` | Starting a non-terminal external program |
| `[CP0-APP] ESC DOWN/UP` | `cp0_lvgl_process.cpp` | Parent process read ESC while an external app was running |
| `[cp0] Returned to launcher` | `cp0_lvgl_process.cpp` | External app exited; preparing to return home |
| `[HOME_STATUS] connected=` | `launch.cpp` | Home status bar refreshed WiFi/battery state |

## 3. Black Screen Troubleshooting

For black screens, first determine whether the process did not start, LVGL did not flush the first frame, asset/page construction crashed, an external app is occupying the framebuffer, or the backlight/brightness is wrong.

### 3.1 Quickly Check Process State

```bash
pgrep -a M5CardputerZero-APPLaunch
systemctl --user status APPLaunch.service --no-pager
journalctl --user -u APPLaunch.service -b --no-pager | tail -120
```

If there is no process:

- Check whether the systemd unit's `ExecStart` path exists.
- Check whether the binary has execute permission.
- Run the binary manually and inspect stderr.

If the process restarts repeatedly:

```bash
journalctl --user -u APPLaunch.service -b --no-pager | grep -Ei 'segfault|assert|error|failed|No such|permission'
```

### 3.2 Check the Startup Log Stage

Different stopping points suggest different directions:

| Stop point | Possible cause | Troubleshooting direction |
| --- | --- | --- |
| No `[BOOT] lv_init()` | Program did not execute or crashed very early | systemd, binary path, dynamic libraries, permissions |
| Stops at `cp0_lvgl_init() starting` | Display/input/audio/hardware initialization stuck | framebuffer, evdev, audio device, hardware HAL |
| `ui_init done` appears but screen is black | First-frame flush failed, backlight is 0, assets make objects invisible | framebuffer, backlight, asset paths |
| Black after entering main loop | Page drawing issue or external app locked the display | Logs, lock file, external process |

### 3.3 Check Framebuffer and Backlight

```bash
ls -l /dev/fb0
id
sudo cat /sys/class/backlight/backlight/brightness
sudo cat /sys/class/backlight/backlight/max_brightness
```

Try increasing brightness:

```bash
echo 80 | sudo tee /sys/class/backlight/backlight/brightness
```

If the Settings page previously saved very low brightness, check:

```bash
sudo grep '^brightness=' /var/lib/applaunch/settings
```

### 3.4 Check Whether an External App Is Occupying the Display

When APPLaunch starts a non-terminal external app, it pauses its own LVGL timer and waits for the child process to exit. If the external app hangs, it may look like Launcher is black-screened or unresponsive.

```bash
ps -eo pid,ppid,pgid,stat,cmd | grep -E 'APPLaunch|Calculator|AppStore|my_app' | grep -v grep
```

First try to terminate the external app process group gracefully, or hold ESC for about 3 seconds so `cp0_process_exec_blocking()` triggers SIGTERM.

### 3.5 Use SDL2 to Narrow the Scope

If the SDL2 build works but the device build is black, prioritize device HAL, framebuffer, backlight, evdev, permissions, and systemd. If SDL2 is also black, prioritize UI construction, asset paths, and LVGL object styles.

## 4. Missing Asset Troubleshooting

Common symptoms of missing assets: blank icons, missing backgrounds, font fallback, no audio, and `missing/unreadable` in logs.

### 4.1 Check Assets in Both Source and Runtime Directories

```bash
# Source tree
find projects/APPLaunch/APPLaunch/share/images -maxdepth 1 -type f | sort | grep my_icon

# Device side
find /usr/share/APPLaunch/share/images -maxdepth 1 -type f | sort | grep my_icon
```

If the source tree has the file but the device does not:

- Rebuild, repackage, and reinstall.
- Check whether `STATIC_FILES += [ADir('../APPLaunch')]` still exists in `projects/APPLaunch/main/SConstruct`.
- Check whether the packaging script copies the `APPLaunch/` asset tree into the package.

### 4.2 Check Path Spelling

Recommended for built-in pages:

```cpp
img_path("my_icon_100.png")
audio_path("key_enter.wav")
```

Recommended for `.desktop`:

```ini
Icon=share/images/my_icon_100.png
```

Do not write development-host absolute paths in device-side pages, such as `/home/nihao/.../projects/APPLaunch/...`. After installation, the device asset root is `/usr/share/APPLaunch/`.

### 4.3 Understand Image Path Special Cases

On the device, `cp0_file_path("xxx.png")` returns `share/images/xxx.png`, which is relative to the current working directory. If you manually start the device binary from an unexpected directory, images may not be found. Run with `/usr/share/APPLaunch` as the working directory, or use the correct systemd `WorkingDirectory`.

On SDL2, APPLaunch automatically probes `APPLaunch/share`, but it is still recommended to run the build output from `projects/APPLaunch`.

### 4.4 Missing Fonts

Check font files:

```bash
find /usr/share/APPLaunch/share/font -maxdepth 1 -type f | sort
```

If adding a font causes crashes or does not display:

- Confirm the extension is `.ttf` or `.otf`.
- Confirm the FreeType build dependency is available.
- First use existing fonts such as `Montserrat-Bold.ttf` or `AlibabaPuHuiTi-3-55-Regular.ttf` to verify page logic.

## 5. Input Not Working

Input failures include the home page not responding, a built-in page not responding, an external app not responding, and incorrect key-code mapping.

### 5.1 Home Page or Built-in Page Not Responding

Check whether the correct input group is bound:

- Home page: `UILaunchPage::bind_home_input_group()`.
- Built-in page: after creating the page, `launch.cpp` calls `lv_indev_set_group(lv_indev_get_next(NULL), p->input_group())`.
- Return home: `Launch::lv_go_back_home()` rebinds the home input group.

When adding events to a built-in page, make sure the event is attached to the correct object and that the object belongs to the page input group. Refer to existing pages' `event_handler_init()` implementations.

### 5.2 Check Whether Device evdev Has Events

```bash
ls -l /dev/input/by-path/platform-3f804000.i2c-event
sudo evtest /dev/input/by-path/platform-3f804000.i2c-event
```

If the default path does not exist, temporarily override it:

```bash
APPLAUNCH_LINUX_KEYBOARD_DEVICE=/dev/input/eventX sudo /usr/share/APPLaunch/bin/M5CardputerZero-APPLaunch
```

### 5.3 Key-code Mapping Issues

Related files:

| File | Purpose |
| --- | --- |
| `ext_components/cp0_lvgl/include/input_keys.h` | Compatible input key definitions |
| `ext_components/cp0_lvgl/include/keyboard_input.h` | APPLaunch private input header |
| `ext_components/cp0_lvgl/include/keyboard_input.h` | cp0_lvgl input interface |
| `ext_components/cp0_lvgl/src/cp0/cp0_lvgl_keyboard.c` | Device-side keyboard input implementation |
| `ext_components/cp0_lvgl/src/sdl/sdl_lvgl_keyboard.c` | SDL2 keyboard input implementation |

Troubleshooting method:

- In SDL2, first confirm that arrow keys, Enter, and Esc trigger the expected behavior.
- On the device, use `evtest` to read raw key codes.
- Compare against `LV_KEY_*` and the project's custom key values.
- During external app execution, check `[CP0-APP] evdev code=... value=...` logs.

### 5.4 External App Input Not Working

After a non-terminal external app starts, APPLaunch calls `keyboard_pause()` to pause its own keyboard thread, but it does not EVIOCGRAB the device. Both the parent and child processes can read the same evdev device. If the external app has no input:

- Confirm the external app reads the same `/dev/input/event*`.
- Confirm the runtime user has permission to read that input device; the external app may be lowered from root to a normal user by default.
- Check the `run_as_user` configuration, or use a fixed built-in registration with `run_as_root`.
- Use `Terminal=true` first to verify whether the command can receive keyboard input in the PTY.

## 6. External App Cannot Return

External apps usually fail to return because the child process does not exit, the process group is not killed, ESC cannot be read from the input device, or the app took over the display and did not restore it.

### 6.1 Normal Return Path

`launch_Exec()` in `launch.cpp`:

1. Shows Loading.
2. Sets `LVGL_RUN_FLAGE = 0`.
3. Unbinds the LVGL input group.
4. Calls `lv_timer_enable(false)` to pause the LVGL timer.
5. Calls `cp0_process_exec_blocking(exec, keep_root)`; ESC state is exchanged through the atomic `cp0_esc_state_*` API.
6. After the child process exits, re-enables the timer, binds the home input group, calls `launch_page_->show_home_screen()` to restore the home screen, and hides Loading.

### 6.2 First Confirm Whether the Child Process Is Still Running

```bash
ps -eo pid,ppid,pgid,stat,cmd | grep -E 'APPLaunch|my_app|sh -c' | grep -v grep
```

If the child process is still running:

- Let the app exit by itself.
- Hold ESC for about 3 seconds.
- Check whether logs contain `[cp0] ESC held ... SIGTERM pgid ...`.

### 6.3 Long ESC Press Does Not Work

Check:

```bash
journalctl --user -u APPLaunch.service -f | grep -E 'CP0-APP|ESC|Returned'
```

If there are no `[CP0-APP] evdev` logs:

- The default keyboard path may be wrong.
- The parent process may not have input-device permission.
- The external app or another process may have exclusive access to the input device.

If there is ESC DOWN but no SIGTERM:

- The hold duration is insufficient; the current threshold is 3 seconds.
- The key code is not `KEY_ESC`; check the keyboard mapping.

### 6.4 Child Exited but Home Page Did Not Restore

Check whether logs contain:

```text
[cp0] Returned to launcher
App ... exited with code ...
```

If return logs exist but the screen is still abnormal:

- The external app may have changed framebuffer state or terminal mode.
- APPLaunch already attempts to force refresh after switching home through `lv_obj_invalidate()` and `lv_refr_now()`; if the screen still does not display, check framebuffer/backlight.
- Check whether the external app left a lock or background process that continues occupying the display.

## 7. Build Failure Troubleshooting

### 7.1 SCons Cannot Find the SDK or Components

Symptom: `project.py`, components, or headers cannot be found.

Troubleshoot:

```bash
cd /home/nihao/w2T/github/launcher/projects/APPLaunch
python3 - <<'PY'
from pathlib import Path
p = Path.cwd()
print('cwd=', p)
print('SDK=', p.parent.parent / 'SDK')
print('ext=', p.parent.parent / 'ext_components')
PY
ls ../../SDK/tools/scons/project.py
ls ../../ext_components/cp0_lvgl
```

APPLaunch's `SConstruct` automatically sets:

```python
SDK_PATH = ../../SDK
EXT_COMPONENTS_PATH = ../../ext_components
```

### 7.2 Missing SDL2 / FreeType / libinput Dependencies

The SDL2 configuration uses `pkg-config` to find `sdl2` and `freetype2`, and links `input`, `xkbcommon`, and `udev`.

Check:

```bash
pkg-config --cflags --libs sdl2 freetype2
ldconfig -p | grep -E 'libinput|libxkbcommon|libudev'
```

Common Ubuntu/Debian dependency package names:

```bash
sudo apt-get install scons pkg-config libsdl2-dev libfreetype-dev libinput-dev libxkbcommon-dev libudev-dev
```

### 7.3 Missing Cross-compilation Sysroot

`linux_x86_cross_cp0_config_defaults.mk` uses `SDK/github_source/static_lib_v0.0.6` as the sysroot. If it does not exist, `SConstruct` tries to download `sdk_bsp.tar.gz`. This fails when network access is restricted.

Check:

```bash
ls -l ../../SDK/github_source/static_lib_v0.0.6
cat ../../SDK/github_source/static_lib_v0.0.6/version 2>/dev/null || true
```

If the download fails, prepare the sysroot ahead of time, or complete one build in a network-enabled environment to populate the cache.

### 7.4 Build Fails After Adding a Page

Common causes:

| Symptom | Cause | Fix |
| --- | --- | --- |
| `PageT not declared` | Page class name and registration name do not match, or `.hpp` was not included by `generated/page_app.h` | Check `generated/page_app.h` and rerun scons |
| SDL2 build cannot find Linux headers | Page directly includes device-only headers | Wrap device-only code with `#if defined(__linux__) && !defined(HAL_PLATFORM_SDL)` |
| Linker cannot find symbols | Functions called by the new page were not added to component dependencies | Check `REQUIREMENTS`/`LDFLAGS` in `main/SConstruct` |
| Duplicate definition | A header-only page defines non-inline global variables/functions | Convert them to class members, `static`, `inline`, or move them into a `.cpp` |

### 7.5 `generated/page_app.h` Auto-generation Changes the Working Tree

`generate_page_app_includes.py` generates `generated/page_app.h` sorted by filename. After adding or deleting `page_app/*.hpp`, a build may modify this file. This is expected, but before committing, confirm that the diff only contains the intended include-list change.

## 8. `.desktop` Load Failure Troubleshooting

### 8.1 File Was Not Scanned

Check:

```bash
ls -l /usr/share/APPLaunch/applications
```

Requirements:

- Filename must end in `.desktop`.
- Content must contain `[Desktop Entry]`.
- At least `Name=` and `Exec=` are required.
- Blank lines and comments beginning with `#` or `;` are skipped.

### 8.2 App Was Skipped by Deduplication

If logs contain:

```text
applications_load: skip ... (duplicate Exec)
```

An existing fixed app or another `.desktop` uses the same `Exec`. Change `Exec` to a unique command.

### 8.3 Icon Does Not Display

The `.desktop` `Icon` field does not automatically call `img_path()`; it is passed as-is to `panel_set_icon()`. Therefore, use:

```ini
Icon=share/images/my_app_100.png
```

If you use an absolute path, also ensure the file exists and is readable on the device.

### 8.4 Command Execution Failed

For terminal apps, verify on the command line first:

```bash
which vim
vim --version
```

For non-terminal apps, check:

```bash
ls -l /usr/share/APPLaunch/bin/my_app
ldd /usr/share/APPLaunch/bin/my_app
sudo -u pi /usr/share/APPLaunch/bin/my_app
```

If APPLaunch starts as root, external apps normally attempt to lower privileges to a normal user. Apps that need root should either be registered as fixed built-in entries with `run_as_root`, or have their program permissions/group permissions adjusted to avoid unnecessary root access.

## 9. Recommended Fault-location Order

1. Run `git status --short` to confirm the current change scope.
2. Build and run SDL2 to eliminate basic UI/syntax issues.
3. Check whether assets exist in both `projects/APPLaunch/APPLaunch` and device `/usr/share/APPLaunch`.
4. Watch `journalctl --user -u APPLaunch.service -f` to identify the startup stage.
5. Use `evtest` to verify the input device and key codes.
6. Use `ps` to inspect external apps and process groups.
7. Check `/var/lib/applaunch/settings` to rule out settings toggles, brightness, or runtime user issues.
8. Finally inspect the HAL layer under `ext_components/cp0_lvgl/src/cp0/`, including framebuffer, keyboard, process, settings, and audio implementations.
