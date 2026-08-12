# 07 - Input System and Key Mapping

This chapter explains APPLaunch's keyboard input thread, the `key_item` event structure, LVGL event dispatch, key mappings on the home screen and built-in pages, terminal input escaping, and debugging notes. The key source files are `ext_components/cp0_lvgl/include/keyboard_input.h`, `projects/APPLaunch/main/ui/ui.h`, `ext_components/cp0_lvgl/src/cp0/cp0_lvgl_keyboard.c`, `ext_components/cp0_lvgl/src/sdl/sdl_lvgl_keyboard.c`, `projects/APPLaunch/main/ui/ui_launch_page.cpp`, and `projects/APPLaunch/main/ui/page_app/*.hpp`.

## 1. Input System Overview

APPLaunch has two input paths:

1. Custom `LV_EVENT_KEYBOARD`: carries the full `struct key_item`; most pages listen to it directly.
2. LVGL indev key: `cp0_keypad_read_cb()` also converts evdev keys to `LV_KEY_*` for LVGL's group/focus mechanism.

Data flow:

```text
Physical keyboard / SDL keyboard
        |
        v
libinput / SDL keyboard backend
        |
        v
keyboard_read_thread()
        |
        v
enqueue_key(struct key_item)
        |
        v
keyboard_queue + keyboard_mutex
        |
        v
cp0_keypad_read_cb()
        |
        +-- lv_obj_send_event(lv_screen_active(), LV_EVENT_KEYBOARD, key_item)
        +-- ui_global_hint_on_key(key_item)
        +-- data->key = cp0_evdev_process_key(key_code)
```

`LV_EVENT_KEYBOARD` is an APPLaunch custom event, not a built-in LVGL key event. It is registered during startup in `main.cpp`:

```cpp
if (LV_EVENT_KEYBOARD == 0)
    LV_EVENT_KEYBOARD = lv_event_register_id();
```

## 2. `key_item` Data Structure

`ext_components/cp0_lvgl/include/keyboard_input.h` defines input events:

```c
struct key_item {
    uint32_t key_code;      // Linux evdev key code
    uint32_t keysym;        // primary XKB keysym
    uint32_t codepoint;     // Unicode code point, 0 if there is no character
    uint32_t mods;          // KBD_MOD_* modifier bitmap
    int      key_state;     // 0=released, 1=pressed, 2=repeat
    char     sym_name[65];  // XKB keysym name
    char     utf8[16];      // UTF-8 character
    char     flage;
    STAILQ_ENTRY(key_item) entries;
};
```

Constants:

| Constant | Value/meaning |
| --- | --- |
| `KBD_KEY_RELEASED` | `0`, released |
| `KBD_KEY_PRESSED` | `1`, pressed |
| `KBD_KEY_REPEATED` | `2`, long-press repeat |
| `KBD_MOD_SHIFT` | Shift modifier |
| `KBD_MOD_CTRL` | Ctrl modifier |
| `KBD_MOD_ALT` | Alt modifier |
| `KBD_MOD_LOGO` | Logo modifier |
| `KBD_MOD_CAPS` | CapsLock state |
| `KBD_MOD_NUM` | NumLock state |

Pages can use `key_code` for physical key checks, or use `utf8` / `codepoint` to read text input.

## 3. Event Macros and Page Access Pattern

`projects/APPLaunch/main/ui/ui.h` provides common macros:

```c
#define LV_EVENT_KEYBOARD_GET_KEY(e) \
    ((struct key_item *)lv_event_get_param(e))->key_code

#define LV_EVENT_KEYBOARD_GET_KEY_STATE(e) \
    ((struct key_item *)lv_event_get_param(e))->key_state

#define IS_KEY_PRESSED(e) \
    ((lv_event_get_code(e) == LV_EVENT_KEYBOARD) && \
     (LV_EVENT_KEYBOARD_GET_KEY_STATE(e) > 0))

#define IS_KEY_RELEASED(e) \
    ((lv_event_get_code(e) == LV_EVENT_KEYBOARD) && \
     (LV_EVENT_KEYBOARD_GET_KEY_STATE(e) == 0))
```

Typical page event binding:

```cpp
void event_handler_init()
{
    lv_obj_add_event_cb(root_screen_, UIIpPanelPage::static_lvgl_handler,
                        LV_EVENT_ALL, this);
}

static void static_lvgl_handler(lv_event_t *e)
{
    auto *self = static_cast<UIIpPanelPage *>(lv_event_get_user_data(e));
    if (!self || !IS_KEY_RELEASED(e))
        return;

    uint32_t key = LV_EVENT_KEYBOARD_GET_KEY(e);
    self->handle_key(key);
}
```

Note: most menu pages handle keys only on release to avoid duplicate triggers from press and repeat. Game-like pages may handle movement and shooting on press/repeat.

## 4. Device-Side Input Thread

The device implementation is in `ext_components/cp0_lvgl/src/cp0/cp0_lvgl_keyboard.c`.

### 4.1 Initialization

`init_input()` does three things:

```c
if (LV_EVENT_KEYBOARD == 0)
    LV_EVENT_KEYBOARD = lv_event_register_id();

pthread_create(&keyboard_read_thread_id, NULL,
               keyboard_read_thread, (void *)keyboard_device);

cp0_create_lvgl_input_devices();
```

The keyboard device defaults to:

```c
const char *keyboard_device = getenv("APPLAUNCH_LINUX_KEYBOARD_DEVICE");
```

If the environment variable is empty, `keyboard_read_thread()` uses this default:

```text
/dev/input/by-path/platform-3f804000.i2c-event
```

This path can also be queried with `cp0_file_path("keyboard_device")`.

### 4.2 Reading and Enqueuing

`keyboard_read_thread()` uses libinput to listen for keyboard events, uses xkbcommon to generate `keysym`, `codepoint`, and `utf8`, and uses timerfd to generate repeat events.

The enqueue function `enqueue_key()`:

```c
static void enqueue_key(const struct key_item *src) {
    struct key_item *elm = malloc(sizeof(*elm));
    *elm = *src;

    if (elm->key_code == KEY_ESC)
        cp0_esc_state_write(elm->key_state);

    if (LVGL_RUN_FLAGE) {
        pthread_mutex_lock(&keyboard_mutex);
        STAILQ_INSERT_TAIL(&keyboard_queue, elm, entries);
        pthread_mutex_unlock(&keyboard_mutex);
    } else {
        free(elm);
    }
}
```

Key global state:

| Variable | Meaning |
| --- | --- |
| `keyboard_queue` | Queue of `key_item` events waiting for LVGL consumption |
| `keyboard_mutex` | Queue lock |
| `cp0_esc_state_*` | Atomic ESC-state C ABI used by long-press return and process-kill logic |
| `LVGL_RUN_FLAGE` | Whether LVGL accepts input; external apps may set it to 0 while running |
| `LV_EVENT_KEYBOARD` | Custom LVGL event id |

### 4.3 Dequeuing and Dispatch

`cp0_keypad_read_cb()` takes events from the queue and dispatches them to the current active screen:

```c
lv_obj_t *root = lv_screen_active();
if (root)
    lv_obj_send_event(root, (lv_event_code_t)LV_EVENT_KEYBOARD, elm);

ui_global_hint_on_key(elm);

data->key = cp0_evdev_process_key(elm->key_code);
if (data->key) {
    data->state = (lv_indev_state_t)elm->key_state;
    data->continue_reading = !STAILQ_EMPTY(&keyboard_queue);
}
free(elm);
```

Note: `elm` is freed after the callback returns, so pages must not keep the pointer returned by `lv_event_get_param(e)` for long-term use. Copy fields if asynchronous use is required.

## 5. evdev to LVGL Key Conversion

`cp0_evdev_process_key()` converts selected Linux evdev keys to LVGL navigation keys:

| evdev key | LVGL key |
| --- | --- |
| `KEY_UP` | `LV_KEY_UP` |
| `KEY_DOWN` | `LV_KEY_DOWN` |
| `KEY_LEFT` | `LV_KEY_LEFT` |
| `KEY_RIGHT` | `LV_KEY_RIGHT` |
| `KEY_ESC` | `LV_KEY_ESC` |
| `KEY_DELETE` | `LV_KEY_DEL` |
| `KEY_BACKSPACE` | `LV_KEY_BACKSPACE` |
| `KEY_ENTER` | `LV_KEY_ENTER` |
| `KEY_NEXT` | `LV_KEY_NEXT` |
| `KEY_PREVIOUS` | `LV_KEY_PREV` |
| `KEY_HOME` | `LV_KEY_HOME` |
| `KEY_END` | `LV_KEY_END` |

If a page handles `LV_EVENT_KEYBOARD` directly, it usually uses the raw `KEY_*` values. If a page delegates to LVGL's widget focus mechanism, it relies on `data->key`.

`ext_components/cp0_lvgl/include/input_keys.h` includes `<linux/input.h>` on Linux and provides common compatible `KEY_*` definitions on non-Linux platforms, so SDL/desktop builds can also compile page code.

## 6. Home Screen Key Mapping

Home screen key handling is in `UILaunchPage::handle_home_key()`; the LVGL C callback entry is `UILaunchPage::on_home_key()` in `projects/APPLaunch/main/ui/ui_launch_page.cpp`.

First, the commonly used `F/X/Z/C` keys on CardputerZero are mapped to arrow keys:

```cpp
static uint32_t fzxc_to_arrow(uint32_t key)
{
    switch (key) {
    case KEY_F: return KEY_UP;
    case KEY_X: return KEY_DOWN;
    case KEY_Z: return KEY_LEFT;
    case KEY_C: return KEY_RIGHT;
    default:    return key;
    }
}
```

Home screen behavior:

| Input | Trigger timing | Behavior |
| --- | --- | --- |
| `KEY_LEFT` or `Z` | pressed/repeat | Play `switch.wav`, call `switch_right()`, and rotate right to the next item |
| `KEY_RIGHT` or `C` | pressed/repeat | Play `switch.wav`, call `switch_left()`, and rotate left to the next item |
| `KEY_ENTER` | released | Play `enter.wav` and launch the current app |
| `KEY_UP` / `KEY_DOWN` or `F` / `X` | pressed/repeat | No action is currently defined on the home screen |

Note: `handle_home_key()` handles left/right keys on press, so a long press may generate repeat events and switch continuously. ENTER launches on release to avoid repeated launches while the key is held down. The log tag still contains `main_key_switch` for compatibility with older debugging output.

## 7. Built-In Page Key Mapping Overview

Each page independently binds `LV_EVENT_KEYBOARD` on its `root_screen_`. Common conventions are:

| Page | File | Main keys |
| --- | --- | --- |
| `UISTPage` | `ui_app_st.hpp` | ESC/arrow/Enter/Backspace are converted to PTY control sequences; HOME-related state is used for exit/external locks |
| `UIGamePage` | `ui_app_game.hpp` | Arrow keys move, ENTER starts/restarts, ESC returns |
| `UISetupPage` | `ui_app_setup.hpp` | UP/DOWN or F/X selects, ENTER/RIGHT or C enters/confirms, ESC/LEFT or Z returns, some pages support R/D |
| `UIGamePage` | `ui_app_game.hpp` | uses the common page key handling; ESC returns |
| `UIIpPanelPage` | `ui_app_ip_panel.hpp` | F/X/Z/C map to LV_KEY_*; UP/DOWN selects; ESC returns |
| `UISSHPage` | `ui_app_ssh.hpp` | UP/DOWN switches Host/Port/User; character input; BACKSPACE deletes; ENTER connects; ESC returns |
| `UILoraPage` | `ui_app_lora.hpp` | Converts KEY_UP/DOWN/LEFT/RIGHT/ENTER/ESC/BACKSPACE/DELETE to LV_KEY_* and passes them to business logic |
| `UITankBattlePage` | `ui_app_tank_battle.hpp` | `33(F)` up, `45(X)` down, `44(Z)` left, `46(C)` right, `57(SPACE)` fire, ESC returns |

## 8. F/X/Z/C Direction-Key Convention

On the CardputerZero keyboard, `F/X/Z/C` are commonly used as arrow-key substitutes. The codebase uses three patterns:

1. Home screen `ui_launch_page.cpp`: `fzxc_to_arrow()` converts `F/X/Z/C` to `KEY_UP/DOWN/LEFT/RIGHT`.
2. Page-local conversion to LVGL keys, for example in `UIGamePage` and `UIIpPanelPage`:

```cpp
switch (key) {
case KEY_F: return LV_KEY_UP;
case KEY_X: return LV_KEY_DOWN;
case KEY_Z: return LV_KEY_LEFT;
case KEY_C: return LV_KEY_RIGHT;
}
```

3. Games directly use evdev numbers: `KEY_MOVE_UP = 33`, `KEY_MOVE_DOWN = 45`, `KEY_MOVE_LEFT = 44`, `KEY_MOVE_RIGHT = 46` in `UITankBattlePage`.

New pages should prefer symbolic names such as `KEY_F` and avoid bare numbers. If numeric values are kept for compatibility with historical hints, comments should state the corresponding key names.

## 9. Text Input

Some pages need character input, such as SSH, Mesh, WiFi passwords, and the terminal.

### 9.1 Simple ASCII Mapping

`UISSHPage` and `UIMeshPage` use `keycode_to_char()` to convert `KEY_1`, `KEY_Q`, and similar keys to lowercase characters:

```cpp
static char keycode_to_char(uint32_t key)
{
    if (key >= KEY_1 && key <= KEY_9) return '1' + (key - KEY_1);
    if (key == KEY_0) return '0';
    if (key >= KEY_Q && key <= KEY_P) return qwerty[key - KEY_Q];
    if (key == KEY_SPACE) return ' ';
    if (key == 52) return '.';  // KEY_DOT
    if (key == 12) return '-';  // KEY_MINUS
    return 0;
}
```

This approach is simple, but it does not support Shift uppercase, input methods, or multibyte characters. For full text input capability, read `key_item::utf8` or `codepoint`.

### 9.2 Terminal Input

`UISTPage` reads `struct key_item` directly and converts physical keys and UTF-8 text to a PTY byte stream:

- `KEY_ENTER` -> `\r`
- `KEY_BACKSPACE` -> `0x7f`
- `KEY_ESC` -> `0x1b`
- Arrow keys -> `\033[A/B/C/D` or `\033OA/OB/OC/OD` in application cursor mode
- Normal characters -> `key_item::utf8`

The terminal page also handles child-process exit, screen refresh, cursor blinking, and ESC/Home return semantics, so it is more complex than ordinary pages.

## 10. Input Handling While External Apps Are Running

External apps are launched through `Launch::launch_Exec()`:

```cpp
LVGL_RUN_FLAGE = 0;
lv_indev_set_group(indev, NULL);
lv_timer_enable(false);

int ret = cp0_process_exec_blocking(exec.c_str(), keep_root ? 1 : 0);

lv_timer_enable(true);
launch_page_->show_home_screen();
LVGL_RUN_FLAGE = 1;
```

Meaning:

- While an external process is running, APPLaunch pauses the LVGL timer and stops receiving normal keyboard queue events.
- Keyboard input updates ESC through `cp0_esc_state_write()`; external-process logic reads it through `cp0_esc_state_read()`.
- After the external process exits, the home screen, input group, and LVGL timer are restored.

## 11. Input Group Switching

The home screen and pages each have their own LVGL group:

- Home screen: `UILaunchPage::home_input_group()`.
- Built-in pages: `AppPageRoot::input_group()`.
- Nested terminal: `UISTPage::input_group()`.

When switching pages, the input group must be switched at the same time:

```cpp
lv_disp_load_scr(p->screen());
lv_indev_set_group(lv_indev_get_next(NULL), p->input_group());
```

Returning to the home screen:

```cpp
launch_page_->show_home_screen();
```

`show_home_screen()` loads the home screen and calls `UILaunchPage::bind_home_input_group()`.

If the screen has switched but the group still points to the old page, the following can occur:

- The visible page does not respond to keys.
- An invisible page still responds to keys.
- ESC behavior is abnormal after exiting a nested page.

## 12. Debugging Input Issues

The device keyboard layer already has logs:

```text
[KBD] enqueue code=... state=... sym=... utf8=... cp=... mods=... run=... home_flag=...
[INDEV] dequeue code=... state=... sym=... utf8=... cp=... active_screen=...
[LAUNCHER] main_key_switch raw=...->code=... state=... sym=...
```

Recommended investigation order:

1. Confirm whether `keyboard_read_thread()` started and whether the device path is correct.
2. Check whether `[KBD] enqueue` appears; if not, the issue is in the libinput/device/xkb layer.
3. Check whether `[INDEV] dequeue` appears; if not, the queue may not be consumed by the LVGL indev.
4. Check whether `active_screen` is the current page's screen.
5. Check whether the page bound `LV_EVENT_KEYBOARD` on `root_screen_`.
6. Check whether the page handles press, release, or repeat, and whether the trigger timing is inconsistent.
7. Check whether `LVGL_RUN_FLAGE` is 0; normal events are discarded while an external app is running.

## 13. Recommendations for New Page Key Handling

New pages should follow these rules:

- List and menu pages: handle `KEY_UP/DOWN/LEFT/RIGHT/ENTER/ESC` on `IS_KEY_RELEASED(e)`.
- Game pages: handle continuous actions on `IS_KEY_PRESSED(e)` and accept repeat if needed.
- Text input pages: prefer `key_item::utf8`; use `keycode_to_char()` only for simple cases.
- Back key: ESC must exit the current page or current popup; for multi-level views, return to the previous level first, then return home.
- Direction substitute keys: if the device keyboard is supported, consistently support `F/X/Z/C`.
- Do not save `struct key_item *` pointers; copy `key_code`, `utf8`, and other fields when asynchronous handling is needed.
- For keys that can be held down, explicitly distinguish `KBD_KEY_PRESSED`, `KBD_KEY_REPEATED`, and `KBD_KEY_RELEASED` to avoid repeated confirmation or repeated launch.
