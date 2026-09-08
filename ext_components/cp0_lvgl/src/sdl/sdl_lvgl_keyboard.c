#include "hal_lvgl_bsp.h"
#include "keyboard_input.h"
#include "commount.h"
#include "lvgl/lvgl.h"
#include "sdl_lvgl.h"
#include "../cp0_keyboard_key_contract.h"
#include "cp0_keyboard_navigation_contract.h"
#include "../cp0_keyboard_input_lifecycle.h"
#include "../cp0_keyboard_queue.h"
#include "../cp0_keyboard_text.h"


#include "lvgl/src/drivers/sdl/lv_sdl_mouse.h"
#include "lvgl/src/drivers/sdl/lv_sdl_private.h"
#include "lvgl/src/drivers/sdl/lv_sdl_window.h"

#include "input_keys.h"
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    struct key_item current;
    SDL_Scancode current_scancode;
    bool current_valid;
    struct key_item active_keys[SDL_NUM_SCANCODES];
    bool active_valid[SDL_NUM_SCANCODES];
} cp0_sdl_keyboard_t;

static void cp0_sdl_keyboard_read(lv_indev_t *indev, lv_indev_data_t *data);
static void cp0_sdl_keyboard_delete_cb(lv_event_t *event);
static cp0_keyboard_input_lifecycle_t keyboard_lifecycle;
static pthread_mutex_t keyboard_init_mutex = PTHREAD_MUTEX_INITIALIZER;
static int mouse_initialized;

__attribute__((weak)) int ui_screensaver_filter_key(const struct key_item *elm)
{
    (void)elm;
    return 0;
}

__attribute__((weak)) void ui_global_hint_on_key(const struct key_item *elm)
{
    (void)elm;
}

static cp0_keyboard_key_handler_t global_key_handler;
typedef int (*cp0_sdl_keyboard_filter_t)(const struct key_item *item);
static cp0_sdl_keyboard_filter_t keyboard_filter;
static volatile int lvgl_keypad_intercept;
static atomic_int keyboard_input_context = KBD_INPUT_CONTEXT_NAVIGATION;

void cp0_keyboard_set_input_context(cp0_keyboard_input_context_t context)
{
    if (context < KBD_INPUT_CONTEXT_NAVIGATION || context > KBD_INPUT_CONTEXT_GAME)
        context = KBD_INPUT_CONTEXT_NAVIGATION;
    atomic_store_explicit(&keyboard_input_context, context, memory_order_release);
}

cp0_keyboard_input_context_t cp0_keyboard_get_input_context(void)
{
    return (cp0_keyboard_input_context_t)atomic_load_explicit(
        &keyboard_input_context, memory_order_acquire);
}

void cp0_keyboard_set_global_key_handler(cp0_keyboard_key_handler_t handler)
{
    global_key_handler = handler;
}

void cp0_sdl_keyboard_set_filter(cp0_sdl_keyboard_filter_t filter)
{
    keyboard_filter = filter;
}

void cp0_keyboard_set_lvgl_keypad_intercept(int intercept)
{
    lvgl_keypad_intercept = intercept != 0;
}

int cp0_keyboard_get_lvgl_keypad_intercept(void)
{
    return lvgl_keypad_intercept;
}

static uint32_t cp0_evdev_process_key(uint16_t code)
{
    switch (code) {
    case KEY_UP:
        return LV_KEY_UP;
    case KEY_DOWN:
        return LV_KEY_DOWN;
    case KEY_RIGHT:
        return LV_KEY_RIGHT;
    case KEY_LEFT:
        return LV_KEY_LEFT;
    case KEY_ESC:
        return LV_KEY_ESC;
    case KEY_DELETE:
        return LV_KEY_DEL;
    case KEY_BACKSPACE:
        return LV_KEY_BACKSPACE;
    case KEY_ENTER:
    case KEY_KPENTER:
        return LV_KEY_ENTER;
    case KEY_NEXT:
        return LV_KEY_NEXT;
    case KEY_TAB:
        return KEY_TAB;
    case KEY_PREVIOUS:
        return LV_KEY_PREV;
    case KEY_HOME:
        return LV_KEY_HOME;
    case KEY_END:
        return LV_KEY_END;
    default:
        return code;
    }
}

static uint32_t cp0_sdl_ctrl_to_lv_key(SDL_Keycode key)
{
    switch (key) {
    case SDLK_RIGHT:
    case SDLK_KP_PLUS:
        return LV_KEY_RIGHT;
    case SDLK_LEFT:
    case SDLK_KP_MINUS:
        return LV_KEY_LEFT;
    case SDLK_UP:
        return LV_KEY_UP;
    case SDLK_DOWN:
        return LV_KEY_DOWN;
    case SDLK_ESCAPE:
        return LV_KEY_ESC;
    case SDLK_BACKSPACE:
        return LV_KEY_BACKSPACE;
    case SDLK_DELETE:
        return LV_KEY_DEL;
    case SDLK_KP_ENTER:
    case SDLK_RETURN:
        return LV_KEY_ENTER;
    case SDLK_TAB:
        return LV_KEY_NEXT;
    case SDLK_PAGEDOWN:
        return LV_KEY_NEXT;
    case SDLK_PAGEUP:
        return LV_KEY_PREV;
    case SDLK_HOME:
        return LV_KEY_HOME;
    case SDLK_END:
        return LV_KEY_END;
    default:
        return 0;
    }
}

static bool cp0_sdl_ctrl_letter(const SDL_KeyboardEvent *event, char *out)
{
    SDL_Keymod mods = SDL_GetModState();
    SDL_Keycode sym = event->keysym.sym;

    if ((mods & KMOD_CTRL) == 0 || sym < SDLK_a || sym > SDLK_z)
        return false;

    *out = (char)(sym - SDLK_a + 1);
    return true;
}

static uint32_t cp0_sdl_scancode_to_linux_key(SDL_Scancode scancode)
{
    switch (scancode) {
    case SDL_SCANCODE_A:
        return KEY_A;
    case SDL_SCANCODE_B:
        return KEY_B;
    case SDL_SCANCODE_C:
        return KEY_C;
    case SDL_SCANCODE_D:
        return KEY_D;
    case SDL_SCANCODE_E:
        return KEY_E;
    case SDL_SCANCODE_F:
        return KEY_F;
    case SDL_SCANCODE_G:
        return KEY_G;
    case SDL_SCANCODE_H:
        return KEY_H;
    case SDL_SCANCODE_I:
        return KEY_I;
    case SDL_SCANCODE_J:
        return KEY_J;
    case SDL_SCANCODE_K:
        return KEY_K;
    case SDL_SCANCODE_L:
        return KEY_L;
    case SDL_SCANCODE_M:
        return KEY_M;
    case SDL_SCANCODE_N:
        return KEY_N;
    case SDL_SCANCODE_O:
        return KEY_O;
    case SDL_SCANCODE_P:
        return KEY_P;
    case SDL_SCANCODE_Q:
        return KEY_Q;
    case SDL_SCANCODE_R:
        return KEY_R;
    case SDL_SCANCODE_S:
        return KEY_S;
    case SDL_SCANCODE_T:
        return KEY_T;
    case SDL_SCANCODE_U:
        return KEY_U;
    case SDL_SCANCODE_V:
        return KEY_V;
    case SDL_SCANCODE_W:
        return KEY_W;
    case SDL_SCANCODE_X:
        return KEY_X;
    case SDL_SCANCODE_Y:
        return KEY_Y;
    case SDL_SCANCODE_Z:
        return KEY_Z;
    case SDL_SCANCODE_1:
        return KEY_1;
    case SDL_SCANCODE_2:
        return KEY_2;
    case SDL_SCANCODE_3:
        return KEY_3;
    case SDL_SCANCODE_4:
        return KEY_4;
    case SDL_SCANCODE_5:
        return KEY_5;
    case SDL_SCANCODE_6:
        return KEY_6;
    case SDL_SCANCODE_7:
        return KEY_7;
    case SDL_SCANCODE_8:
        return KEY_8;
    case SDL_SCANCODE_9:
        return KEY_9;
    case SDL_SCANCODE_0:
        return KEY_0;
    case SDL_SCANCODE_RETURN:
        return KEY_ENTER;
    case SDL_SCANCODE_KP_ENTER:
        return KEY_KPENTER;
    case SDL_SCANCODE_ESCAPE:
        return KEY_ESC;
    case SDL_SCANCODE_BACKSPACE:
        return KEY_BACKSPACE;
    case SDL_SCANCODE_TAB:
        return KEY_TAB;
    case SDL_SCANCODE_SPACE:
        return KEY_SPACE;
    case SDL_SCANCODE_MINUS:
        return KEY_MINUS;
    case SDL_SCANCODE_EQUALS:
        return KEY_EQUAL;
    case SDL_SCANCODE_LEFTBRACKET:
        return KEY_LEFTBRACE;
    case SDL_SCANCODE_RIGHTBRACKET:
        return KEY_RIGHTBRACE;
    case SDL_SCANCODE_BACKSLASH:
        return KEY_BACKSLASH;
    case SDL_SCANCODE_SEMICOLON:
        return KEY_SEMICOLON;
    case SDL_SCANCODE_APOSTROPHE:
        return KEY_APOSTROPHE;
    case SDL_SCANCODE_GRAVE:
        return KEY_GRAVE;
    case SDL_SCANCODE_COMMA:
        return KEY_COMMA;
    case SDL_SCANCODE_PERIOD:
        return KEY_DOT;
    case SDL_SCANCODE_SLASH:
        return KEY_SLASH;
    case SDL_SCANCODE_CAPSLOCK:
        return KEY_CAPSLOCK;
    case SDL_SCANCODE_F1:
        return KEY_F1;
    case SDL_SCANCODE_F2:
        return KEY_F2;
    case SDL_SCANCODE_F3:
        return KEY_F3;
    case SDL_SCANCODE_F4:
        return KEY_F4;
    case SDL_SCANCODE_F5:
        return KEY_F5;
    case SDL_SCANCODE_F6:
        return KEY_F6;
    case SDL_SCANCODE_F7:
        return KEY_F7;
    case SDL_SCANCODE_F8:
        return KEY_F8;
    case SDL_SCANCODE_F9:
        return KEY_F9;
    case SDL_SCANCODE_F10:
        return KEY_F10;
    case SDL_SCANCODE_F11:
        return KEY_F11;
    case SDL_SCANCODE_F12:
        return KEY_F12;
    case SDL_SCANCODE_PRINTSCREEN:
        return KEY_SYSRQ;
    case SDL_SCANCODE_INSERT:
        return KEY_INSERT;
    case SDL_SCANCODE_HOME:
        return KEY_HOME;
    case SDL_SCANCODE_PAGEUP:
        return KEY_PAGEUP;
    case SDL_SCANCODE_DELETE:
        return KEY_DELETE;
    case SDL_SCANCODE_END:
        return KEY_END;
    case SDL_SCANCODE_PAGEDOWN:
        return KEY_PAGEDOWN;
    case SDL_SCANCODE_RIGHT:
        return KEY_RIGHT;
    case SDL_SCANCODE_LEFT:
        return KEY_LEFT;
    case SDL_SCANCODE_DOWN:
        return KEY_DOWN;
    case SDL_SCANCODE_UP:
        return KEY_UP;
    case SDL_SCANCODE_LCTRL:
        return KEY_LEFTCTRL;
    case SDL_SCANCODE_LSHIFT:
        return KEY_LEFTSHIFT;
    case SDL_SCANCODE_LALT:
        return KEY_LEFTALT;
    case SDL_SCANCODE_LGUI:
        return KEY_LEFTMETA;
    case SDL_SCANCODE_RCTRL:
        return KEY_RIGHTCTRL;
    case SDL_SCANCODE_RSHIFT:
        return KEY_RIGHTSHIFT;
    case SDL_SCANCODE_RALT:
        return KEY_RIGHTALT;
    case SDL_SCANCODE_RGUI:
        return KEY_RIGHTMETA;
    default:
        return (uint32_t)scancode;
    }
}

struct keyboard_queue_t keyboard_queue;
pthread_mutex_t keyboard_mutex = PTHREAD_MUTEX_INITIALIZER;
volatile int LVGL_RUN_FLAGE = 1;
volatile uint32_t LV_EVENT_KEYBOARD;

void keyboard_pause(void) {}
void keyboard_resume(void) {}
void *keyboard_read_thread(void *argv)
{
    (void)argv;
    return NULL;
}
void kbd_dump_keymap_table(void) {}

const char *kbd_state_name(int state)
{
    switch (state) {
    case KBD_KEY_RELEASED:
        return "UP";
    case KBD_KEY_PRESSED:
        return "DOWN";
    case KBD_KEY_REPEATED:
        return "REPEAT";
    default:
        return "???";
    }
}

static void cp0_sdl_enqueue_key(const struct key_item *event)
{
    (void)cp0_keyboard_queue_push(event);
}

int cp0_keyboard_inject(uint32_t key_code, int key_state, uint32_t mods)
{
    if (key_state != KBD_KEY_RELEASED && key_state != KBD_KEY_PRESSED &&
        key_state != KBD_KEY_REPEATED)
        return -1;
    struct key_item item = {0};
    item.key_code = key_code;
    item.key_state = key_state;
    item.mods = mods;
    item.input_context = cp0_keyboard_get_input_context();
    item.semantic_key = cp0_keyboard_semantic_key(key_code, item.input_context);
    const char *control = cp0_keyboard_control_utf8(key_code);
    if (control) snprintf(item.utf8, sizeof(item.utf8), "%s", control);
    snprintf(item.sym_name, sizeof(item.sym_name), "RPC_%u", key_code);
    return cp0_keyboard_queue_push(&item);
}

int cp0_keyboard_inject_text(const char *utf8)
{
    if (!utf8 || cp0_keyboard_utf8_validate(utf8) != 0) return -1;
    const char *cursor = utf8;
    while (*cursor) {
        struct key_item item = {0};
        size_t length = 0;
        if (cp0_keyboard_utf8_decode_one(cursor, &item.codepoint, &length) != 0) return -1;
        item.key_state = KBD_KEY_RELEASED;
        memcpy(item.utf8, cursor, length);
        snprintf(item.sym_name, sizeof(item.sym_name), "RPC_TEXT");
        if (cp0_keyboard_queue_push(&item) != 0) return -1;
        cursor += length;
    }
    return 0;
}

static void cp0_sdl_fill_key_meta(cp0_sdl_keyboard_t *kbd, const SDL_KeyboardEvent *event)
{
    SDL_Keycode sym = event->keysym.sym;
    SDL_Scancode scancode = event->keysym.scancode;
    SDL_Keymod mods = SDL_GetModState();
    const char *name = SDL_GetKeyName(sym);

    memset(&kbd->current, 0, sizeof(kbd->current));
    kbd->current_scancode = scancode;
    kbd->current.key_code = cp0_sdl_scancode_to_linux_key(scancode);
    kbd->current.input_context = cp0_keyboard_get_input_context();
    kbd->current.semantic_key = cp0_keyboard_semantic_key(
        kbd->current.key_code, kbd->current.input_context);
    kbd->current.keysym = (uint32_t)sym;
    kbd->current.mods = 0;
    kbd->current.key_state = event->repeat ? KBD_KEY_REPEATED : KBD_KEY_PRESSED;
    kbd->current.codepoint = cp0_sdl_ctrl_to_lv_key(sym);
    if (mods & KMOD_SHIFT)
        kbd->current.mods |= KBD_MOD_SHIFT;
    if (mods & KMOD_CTRL)
        kbd->current.mods |= KBD_MOD_CTRL;
    if (mods & KMOD_ALT)
        kbd->current.mods |= KBD_MOD_ALT;
    if (mods & KMOD_GUI)
        kbd->current.mods |= KBD_MOD_LOGO;
    if (name != NULL)
        snprintf(kbd->current.sym_name, sizeof(kbd->current.sym_name), "%s", name);

    const char *ctrl_utf8 = cp0_keyboard_control_utf8(kbd->current.key_code);
    if (ctrl_utf8 != NULL)
        snprintf(kbd->current.utf8, sizeof(kbd->current.utf8), "%s", ctrl_utf8);

    kbd->current_valid = true;
}

static void cp0_sdl_set_text_key(cp0_sdl_keyboard_t *kbd, const char *utf8)
{
    if (!kbd->current_valid) {
        memset(&kbd->current, 0, sizeof(kbd->current));
        kbd->current.key_state = KBD_KEY_PRESSED;
        kbd->current_valid = true;
    }

    (void)cp0_keyboard_utf8_copy(kbd->current.utf8, sizeof(kbd->current.utf8), utf8);
    size_t decoded_length = 0;
    if (cp0_keyboard_utf8_decode_one(
            kbd->current.utf8, &kbd->current.codepoint, &decoded_length) != 0)
        kbd->current.codepoint = 0;
}

static void cp0_sdl_remember_active_key(cp0_sdl_keyboard_t *kbd, SDL_Scancode scancode,
                                        const struct key_item *item)
{
    if (scancode < SDL_NUM_SCANCODES) {
        kbd->active_keys[scancode] = *item;
        kbd->active_valid[scancode] = true;
    }
}

static lv_indev_t *cp0_sdl_keyboard_create(void)
{
    cp0_sdl_keyboard_t *kbd = calloc(1, sizeof(*kbd));
    if (kbd == NULL)
        return NULL;

    lv_indev_t *indev = lv_indev_create();
    if (indev == NULL) {
        free(kbd);
        return NULL;
    }

    lv_indev_set_type(indev, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(indev, cp0_sdl_keyboard_read);
    lv_indev_set_driver_data(indev, kbd);
    lv_indev_set_mode(indev, LV_INDEV_MODE_EVENT);
    lv_indev_add_event_cb(indev, cp0_sdl_keyboard_delete_cb, LV_EVENT_DELETE, indev);
    return indev;
}

static void cp0_sdl_keyboard_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;

    data->key = 0;
    data->state = LV_INDEV_STATE_RELEASED;
    data->continue_reading = false;

    struct key_item *elm = cp0_keyboard_queue_pop();
    if (elm) {
        const int intercept = lvgl_keypad_intercept;

        int swallowed = keyboard_filter
            ? keyboard_filter(elm)
            : ui_screensaver_filter_key(elm);
        if (getenv("EMU_DEBUG_INPUT") != NULL)
            fprintf(stderr,
                    "[EMU] Keyboard filter key=%u state=%d handler=%s consumed=%d\n",
                    elm->key_code, elm->key_state,
                    keyboard_filter ? "app" : "fallback", swallowed);
        if (!swallowed) {
            lv_obj_t *root = lv_screen_active();
            if (root != NULL)
                lv_obj_send_event(root, (lv_event_code_t)LV_EVENT_KEYBOARD, elm);

            if (global_key_handler)
                global_key_handler(elm);
            else
                ui_global_hint_on_key(elm);

            if (!intercept) {
                const uint32_t semantic_key = elm->semantic_key
                                                  ? elm->semantic_key
                                                  : elm->key_code;
                data->key = cp0_evdev_process_key(semantic_key);
                if (data->key)
                    data->state = (lv_indev_state_t)elm->key_state;
            }
        }
        data->continue_reading = cp0_keyboard_queue_has_data();
        free(elm);
    }
}

static void cp0_sdl_keyboard_delete_cb(lv_event_t *event)
{
    lv_indev_t *indev = (lv_indev_t *)lv_event_get_user_data(event);
    cp0_sdl_keyboard_t *kbd = lv_indev_get_driver_data(indev);
    if (kbd != NULL) {
        lv_indev_set_driver_data(indev, NULL);
        lv_indev_set_read_cb(indev, NULL);
        free(kbd);
        cp0_keyboard_queue_clear();
    }
    pthread_mutex_lock(&keyboard_init_mutex);
    cp0_keyboard_input_handle_deleted(&keyboard_lifecycle, indev);
    pthread_mutex_unlock(&keyboard_init_mutex);
}

void lv_sdl_keyboard_handler(SDL_Event *event)
{
    if (event == NULL) return;
    uint32_t win_id = UINT32_MAX;
    switch (event->type) {
    case SDL_KEYDOWN:
    case SDL_KEYUP:
        win_id = event->key.windowID;
        break;
    case SDL_TEXTINPUT:
        win_id = event->text.windowID;
        break;
    default:
        return;
    }

    lv_display_t *disp = lv_sdl_get_disp_from_win_id(win_id);
    lv_indev_t *indev = lv_indev_get_next(NULL);
    while (indev != NULL) {
        if (lv_indev_get_read_cb(indev) == cp0_sdl_keyboard_read) {
            if (disp == NULL || lv_indev_get_display(indev) == disp)
                break;
        }
        indev = lv_indev_get_next(indev);
    }
    if (indev == NULL)
        return;

    cp0_sdl_keyboard_t *kbd = lv_indev_get_driver_data(indev);
    if (kbd == NULL) return;
    if (event->type == SDL_KEYDOWN) {
        cp0_sdl_fill_key_meta(kbd, &event->key);
        uint32_t ctrl_key = cp0_sdl_ctrl_to_lv_key(event->key.keysym.sym);
        char ctrl_char = 0;
        if (ctrl_key != 0 || cp0_sdl_ctrl_letter(&event->key, &ctrl_char)) {
            if (ctrl_char != 0) {
                kbd->current.utf8[0] = ctrl_char;
                kbd->current.utf8[1] = '\0';
                kbd->current.codepoint = (uint32_t)ctrl_char;
            }
            cp0_sdl_enqueue_key(&kbd->current);
            cp0_sdl_remember_active_key(kbd, event->key.keysym.scancode, &kbd->current);
            kbd->current_valid = false;
        }
    }
    else if (event->type == SDL_TEXTINPUT) {
        cp0_sdl_set_text_key(kbd, event->text.text);
        cp0_sdl_enqueue_key(&kbd->current);
        cp0_sdl_remember_active_key(kbd, kbd->current_scancode, &kbd->current);
        kbd->current_valid = false;
    }
    else if (event->type == SDL_KEYUP) {
        SDL_Scancode scancode = event->key.keysym.scancode;
        struct key_item item;
        if (scancode < SDL_NUM_SCANCODES && kbd->active_valid[scancode]) {
            item = kbd->active_keys[scancode];
            kbd->active_valid[scancode] = false;
        }
        else {
            cp0_sdl_fill_key_meta(kbd, &event->key);
            item = kbd->current;
        }
        item.key_state = KBD_KEY_RELEASED;
        cp0_sdl_enqueue_key(&item);
        if (kbd->current_scancode == scancode)
            kbd->current_valid = false;
    }

    while (cp0_keyboard_queue_has_data())
        lv_indev_read(indev);
}


void init_sdl_input(void)
{
    pthread_mutex_lock(&keyboard_init_mutex);
    if (!cp0_keyboard_input_begin_create(&keyboard_lifecycle)) {
        pthread_mutex_unlock(&keyboard_init_mutex);
        return;
    }

    cp0_keyboard_queue_init();
    if (LV_EVENT_KEYBOARD == 0)
        LV_EVENT_KEYBOARD = lv_event_register_id();

    if (!mouse_initialized) {
        lv_sdl_mouse_create();
        mouse_initialized = 1;
    }
    lv_indev_t *indev = cp0_sdl_keyboard_create();
    cp0_keyboard_input_finish_create(&keyboard_lifecycle, indev);
    if (indev == NULL)
        fprintf(stderr, "cp0_lvgl: failed to create SDL keyboard input\n");

    pthread_mutex_unlock(&keyboard_init_mutex);
}

void init_input(void)
{
    init_sdl_input();
}

void deinit_input(void)
{
}
