#include "cp0_keyboard_lvgl_input.h"
#include "../cp0_keyboard_queue.h"
#include "../cp0_keyboard_input_lifecycle.h"

#include "input_keys.h"
#include "keyboard_input.h"
#include "lvgl/lvgl.h"
#include "../../../../SDK/components/utilities/include/sample_log.h"

#include <stdio.h>
#include <stdlib.h>

#undef SLOGD
#define SLOGD(...) do { } while (0)

static cp0_keyboard_key_handler_t global_key_handler;
static cp0_keyboard_input_lifecycle_t keypad_lifecycle;
static volatile int lvgl_keypad_intercept;

static void keypad_delete_cb(lv_event_t *event)
{
    cp0_keyboard_input_handle_deleted(
        &keypad_lifecycle, lv_event_get_user_data(event));
}

__attribute__((weak)) void ui_global_hint_on_key(const struct key_item *item)
{
    (void)item;
}

__attribute__((weak)) int ui_screensaver_filter_key(const struct key_item *item)
{
    (void)item;
    return 0;
}

void cp0_keyboard_set_global_key_handler(cp0_keyboard_key_handler_t handler)
{
    global_key_handler = handler;
}

void cp0_keyboard_set_lvgl_keypad_intercept(int intercept)
{
    lvgl_keypad_intercept = intercept != 0;
}

int cp0_keyboard_get_lvgl_keypad_intercept(void)
{
    return lvgl_keypad_intercept;
}

static uint32_t lvgl_key_from_evdev(uint16_t code)
{
    switch (code) {
    case KEY_UP: return LV_KEY_UP;
    case KEY_DOWN: return LV_KEY_DOWN;
    case KEY_RIGHT: return LV_KEY_RIGHT;
    case KEY_LEFT: return LV_KEY_LEFT;
    case KEY_ESC: return LV_KEY_ESC;
    case KEY_DELETE: return LV_KEY_DEL;
    case KEY_BACKSPACE: return LV_KEY_BACKSPACE;
    case KEY_ENTER: return LV_KEY_ENTER;
    case KEY_NEXT: return LV_KEY_NEXT;
    case KEY_TAB: return KEY_TAB;
    case KEY_PREVIOUS: return LV_KEY_PREV;
    case KEY_HOME: return LV_KEY_HOME;
    case KEY_END: return LV_KEY_END;
    default: return code;
    }
}

static void keypad_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    data->key = 0;
    data->state = LV_INDEV_STATE_RELEASED;
    data->continue_reading = false;

    struct key_item *item = cp0_keyboard_queue_pop();
    if (item) {
        const int intercept = lvgl_keypad_intercept;

        char utf8_debug[64] = "";
        int debug_length = 0;
        for (int i = 0; i < (int)sizeof(item->utf8) && item->utf8[i] && debug_length < 60; ++i) {
            unsigned char value = (unsigned char)item->utf8[i];
            if (value >= 0x20 && value < 0x7f)
                utf8_debug[debug_length++] = (char)value;
            else
                debug_length += snprintf(utf8_debug + debug_length,
                                         sizeof(utf8_debug) - (size_t)debug_length,
                                         "\\x%02x", value);
        }
        utf8_debug[debug_length] = '\0';
        SLOGD("[INDEV] dequeue code=%u state=%s sym=%s utf8='%s' cp=0x%x active_screen=%p",
              item->key_code, kbd_state_name(item->key_state), item->sym_name,
              utf8_debug, item->codepoint, (void *)lv_screen_active());

        int swallowed = ui_screensaver_filter_key(item);
        if (!swallowed) {
            lv_obj_t *root = lv_screen_active();
            if (root)
                lv_obj_send_event(root, (lv_event_code_t)LV_EVENT_KEYBOARD, item);
            if (global_key_handler)
                global_key_handler(item);
            else
                ui_global_hint_on_key(item);

            if (!intercept) {
                data->key = lvgl_key_from_evdev((uint16_t)item->key_code);
                if (data->key)
                    data->state = (lv_indev_state_t)item->key_state;
            }
        }
        data->continue_reading = cp0_keyboard_queue_has_data();
        free(item);
    }
}

void cp0_keyboard_create_lvgl_input_devices(void)
{
#if LV_USE_EVDEV
    const char *mouse_device = getenv("LV_LINUX_MOUSE_DEVICE");
    if (mouse_device && mouse_device[0] != '\0')
        lv_evdev_create(LV_INDEV_TYPE_POINTER, mouse_device);
#endif

    if (!cp0_keyboard_input_begin_create(&keypad_lifecycle)) return;
    lv_indev_t *indev = lv_indev_create();
    if (indev) {
        lv_indev_set_type(indev, LV_INDEV_TYPE_KEYPAD);
        lv_indev_set_read_cb(indev, keypad_read);
        lv_indev_add_event_cb(indev, keypad_delete_cb, LV_EVENT_DELETE, indev);
    }
    cp0_keyboard_input_finish_create(&keypad_lifecycle, indev);
}
