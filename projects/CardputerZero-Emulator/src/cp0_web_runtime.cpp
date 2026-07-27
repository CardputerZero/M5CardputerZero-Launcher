#include "cp0_lvgl_app.h"
#include "keyboard_input.h"
#include "lvgl/lvgl.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <pthread.h>
#include <string>
#include <sys/queue.h>

#ifndef KEY_ESC
#define KEY_ESC 1
#define KEY_1 2
#define KEY_2 3
#define KEY_3 4
#define KEY_4 5
#define KEY_5 6
#define KEY_6 7
#define KEY_7 8
#define KEY_8 9
#define KEY_9 10
#define KEY_0 11
#define KEY_MINUS 12
#define KEY_EQUAL 13
#define KEY_BACKSPACE 14
#define KEY_TAB 15
#define KEY_Q 16
#define KEY_W 17
#define KEY_E 18
#define KEY_R 19
#define KEY_T 20
#define KEY_Y 21
#define KEY_U 22
#define KEY_I 23
#define KEY_O 24
#define KEY_P 25
#define KEY_LEFTBRACE 26
#define KEY_RIGHTBRACE 27
#define KEY_ENTER 28
#define KEY_LEFTCTRL 29
#define KEY_A 30
#define KEY_S 31
#define KEY_D 32
#define KEY_F 33
#define KEY_G 34
#define KEY_H 35
#define KEY_J 36
#define KEY_K 37
#define KEY_L 38
#define KEY_SEMICOLON 39
#define KEY_APOSTROPHE 40
#define KEY_GRAVE 41
#define KEY_LEFTSHIFT 42
#define KEY_BACKSLASH 43
#define KEY_Z 44
#define KEY_X 45
#define KEY_C 46
#define KEY_V 47
#define KEY_B 48
#define KEY_N 49
#define KEY_M 50
#define KEY_COMMA 51
#define KEY_DOT 52
#define KEY_SLASH 53
#define KEY_LEFTALT 56
#define KEY_SPACE 57
#define KEY_F1 59
#define KEY_F2 60
#define KEY_F3 61
#define KEY_F4 62
#define KEY_F5 63
#define KEY_F6 64
#define KEY_F7 65
#define KEY_F8 66
#define KEY_F9 67
#define KEY_F10 68
#define KEY_F11 87
#define KEY_F12 88
#define KEY_HOME 102
#define KEY_UP 103
#define KEY_PAGEUP 104
#define KEY_LEFT 105
#define KEY_RIGHT 106
#define KEY_END 107
#define KEY_DOWN 108
#define KEY_PAGEDOWN 109
#define KEY_INSERT 110
#define KEY_DELETE 111
#define KEY_RIGHTCTRL 97
#define KEY_RIGHTALT 100
#endif

struct keyboard_queue_t keyboard_queue;
pthread_mutex_t keyboard_mutex = PTHREAD_MUTEX_INITIALIZER;
volatile int LVGL_HOME_KEY_FLAG = 0;
volatile int LVGL_RUN_FLAGE = 1;
volatile uint32_t LV_EVENT_KEYBOARD = 0;

namespace {

struct WebKeyboard {
    key_item current{};
    SDL_Scancode current_scancode{};
    bool current_valid = false;
    key_item active_keys[SDL_NUM_SCANCODES]{};
    bool active_valid[SDL_NUM_SCANCODES]{};
};

uint32_t utf8_first_codepoint(const char *s)
{
    const unsigned char *p = reinterpret_cast<const unsigned char *>(s);
    if (!p || p[0] == '\0') return 0;
    if (p[0] < 0x80) return p[0];
    if ((p[0] & 0xe0) == 0xc0 && (p[1] & 0xc0) == 0x80)
        return ((p[0] & 0x1f) << 6) | (p[1] & 0x3f);
    if ((p[0] & 0xf0) == 0xe0 && (p[1] & 0xc0) == 0x80 && (p[2] & 0xc0) == 0x80)
        return ((p[0] & 0x0f) << 12) | ((p[1] & 0x3f) << 6) | (p[2] & 0x3f);
    if ((p[0] & 0xf8) == 0xf0 && (p[1] & 0xc0) == 0x80 && (p[2] & 0xc0) == 0x80 && (p[3] & 0xc0) == 0x80)
        return ((p[0] & 0x07) << 18) | ((p[1] & 0x3f) << 12) | ((p[2] & 0x3f) << 6) | (p[3] & 0x3f);
    return 0;
}

uint32_t linux_key_to_lv(uint32_t code)
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
    case KEY_TAB: return LV_KEY_NEXT;
    case KEY_PAGEUP: return LV_KEY_PREV;
    case KEY_PAGEDOWN: return LV_KEY_NEXT;
    case KEY_HOME: return LV_KEY_HOME;
    case KEY_END: return LV_KEY_END;
    default: return code;
    }
}

uint32_t ctrl_to_lv_key(SDL_Keycode key)
{
    switch (key) {
    case SDLK_RIGHT: return LV_KEY_RIGHT;
    case SDLK_LEFT: return LV_KEY_LEFT;
    case SDLK_UP: return LV_KEY_UP;
    case SDLK_DOWN: return LV_KEY_DOWN;
    case SDLK_ESCAPE: return LV_KEY_ESC;
    case SDLK_BACKSPACE: return LV_KEY_BACKSPACE;
    case SDLK_DELETE: return LV_KEY_DEL;
    case SDLK_RETURN:
    case SDLK_KP_ENTER: return LV_KEY_ENTER;
    case SDLK_TAB:
    case SDLK_PAGEDOWN: return LV_KEY_NEXT;
    case SDLK_PAGEUP: return LV_KEY_PREV;
    case SDLK_HOME: return LV_KEY_HOME;
    case SDLK_END: return LV_KEY_END;
    default: return 0;
    }
}

const char *ctrl_utf8(SDL_Keycode key)
{
    switch (key) {
    case SDLK_RETURN:
    case SDLK_KP_ENTER: return "\r";
    case SDLK_BACKSPACE: return "\x7f";
    case SDLK_TAB: return "\t";
    case SDLK_ESCAPE: return "\x1b";
    case SDLK_UP: return "\033[A";
    case SDLK_DOWN: return "\033[B";
    case SDLK_RIGHT: return "\033[C";
    case SDLK_LEFT: return "\033[D";
    case SDLK_HOME: return "\033[H";
    case SDLK_END: return "\033[F";
    case SDLK_DELETE: return "\033[3~";
    case SDLK_PAGEUP: return "\033[5~";
    case SDLK_PAGEDOWN: return "\033[6~";
    case SDLK_F1: return "\033OP";
    case SDLK_F2: return "\033OQ";
    case SDLK_F3: return "\033OR";
    case SDLK_F4: return "\033OS";
    case SDLK_F5: return "\033[15~";
    case SDLK_F6: return "\033[17~";
    case SDLK_F7: return "\033[18~";
    case SDLK_F8: return "\033[19~";
    case SDLK_F9: return "\033[20~";
    case SDLK_F10: return "\033[21~";
    case SDLK_F11: return "\033[23~";
    case SDLK_F12: return "\033[24~";
    default: return nullptr;
    }
}

uint32_t scancode_to_linux_key(SDL_Scancode scancode)
{
    switch (scancode) {
    case SDL_SCANCODE_A: return KEY_A;
    case SDL_SCANCODE_B: return KEY_B;
    case SDL_SCANCODE_C: return KEY_C;
    case SDL_SCANCODE_D: return KEY_D;
    case SDL_SCANCODE_E: return KEY_E;
    case SDL_SCANCODE_F: return KEY_F;
    case SDL_SCANCODE_G: return KEY_G;
    case SDL_SCANCODE_H: return KEY_H;
    case SDL_SCANCODE_I: return KEY_I;
    case SDL_SCANCODE_J: return KEY_J;
    case SDL_SCANCODE_K: return KEY_K;
    case SDL_SCANCODE_L: return KEY_L;
    case SDL_SCANCODE_M: return KEY_M;
    case SDL_SCANCODE_N: return KEY_N;
    case SDL_SCANCODE_O: return KEY_O;
    case SDL_SCANCODE_P: return KEY_P;
    case SDL_SCANCODE_Q: return KEY_Q;
    case SDL_SCANCODE_R: return KEY_R;
    case SDL_SCANCODE_S: return KEY_S;
    case SDL_SCANCODE_T: return KEY_T;
    case SDL_SCANCODE_U: return KEY_U;
    case SDL_SCANCODE_V: return KEY_V;
    case SDL_SCANCODE_W: return KEY_W;
    case SDL_SCANCODE_X: return KEY_X;
    case SDL_SCANCODE_Y: return KEY_Y;
    case SDL_SCANCODE_Z: return KEY_Z;
    case SDL_SCANCODE_1: return KEY_1;
    case SDL_SCANCODE_2: return KEY_2;
    case SDL_SCANCODE_3: return KEY_3;
    case SDL_SCANCODE_4: return KEY_4;
    case SDL_SCANCODE_5: return KEY_5;
    case SDL_SCANCODE_6: return KEY_6;
    case SDL_SCANCODE_7: return KEY_7;
    case SDL_SCANCODE_8: return KEY_8;
    case SDL_SCANCODE_9: return KEY_9;
    case SDL_SCANCODE_0: return KEY_0;
    case SDL_SCANCODE_RETURN: return KEY_ENTER;
    case SDL_SCANCODE_ESCAPE: return KEY_ESC;
    case SDL_SCANCODE_BACKSPACE: return KEY_BACKSPACE;
    case SDL_SCANCODE_TAB: return KEY_TAB;
    case SDL_SCANCODE_SPACE: return KEY_SPACE;
    case SDL_SCANCODE_MINUS: return KEY_MINUS;
    case SDL_SCANCODE_EQUALS: return KEY_EQUAL;
    case SDL_SCANCODE_LEFTBRACKET: return KEY_LEFTBRACE;
    case SDL_SCANCODE_RIGHTBRACKET: return KEY_RIGHTBRACE;
    case SDL_SCANCODE_BACKSLASH: return KEY_BACKSLASH;
    case SDL_SCANCODE_SEMICOLON: return KEY_SEMICOLON;
    case SDL_SCANCODE_APOSTROPHE: return KEY_APOSTROPHE;
    case SDL_SCANCODE_GRAVE: return KEY_GRAVE;
    case SDL_SCANCODE_COMMA: return KEY_COMMA;
    case SDL_SCANCODE_PERIOD: return KEY_DOT;
    case SDL_SCANCODE_SLASH: return KEY_SLASH;
    case SDL_SCANCODE_F1: return KEY_F1;
    case SDL_SCANCODE_F2: return KEY_F2;
    case SDL_SCANCODE_F3: return KEY_F3;
    case SDL_SCANCODE_F4: return KEY_F4;
    case SDL_SCANCODE_F5: return KEY_F5;
    case SDL_SCANCODE_F6: return KEY_F6;
    case SDL_SCANCODE_F7: return KEY_F7;
    case SDL_SCANCODE_F8: return KEY_F8;
    case SDL_SCANCODE_F9: return KEY_F9;
    case SDL_SCANCODE_F10: return KEY_F10;
    case SDL_SCANCODE_F11: return KEY_F11;
    case SDL_SCANCODE_F12: return KEY_F12;
    case SDL_SCANCODE_INSERT: return KEY_INSERT;
    case SDL_SCANCODE_HOME: return KEY_HOME;
    case SDL_SCANCODE_PAGEUP: return KEY_PAGEUP;
    case SDL_SCANCODE_DELETE: return KEY_DELETE;
    case SDL_SCANCODE_END: return KEY_END;
    case SDL_SCANCODE_PAGEDOWN: return KEY_PAGEDOWN;
    case SDL_SCANCODE_RIGHT: return KEY_RIGHT;
    case SDL_SCANCODE_LEFT: return KEY_LEFT;
    case SDL_SCANCODE_DOWN: return KEY_DOWN;
    case SDL_SCANCODE_UP: return KEY_UP;
    case SDL_SCANCODE_LCTRL: return KEY_LEFTCTRL;
    case SDL_SCANCODE_RCTRL: return KEY_RIGHTCTRL;
    case SDL_SCANCODE_LSHIFT: return KEY_LEFTSHIFT;
    case SDL_SCANCODE_LALT: return KEY_LEFTALT;
    case SDL_SCANCODE_RALT: return KEY_RIGHTALT;
    default: return static_cast<uint32_t>(scancode);
    }
}

void fill_key_meta(WebKeyboard *kbd, const SDL_KeyboardEvent *event)
{
    std::memset(&kbd->current, 0, sizeof(kbd->current));
    kbd->current_scancode = event->keysym.scancode;
    kbd->current.key_code = scancode_to_linux_key(event->keysym.scancode);
    kbd->current.keysym = static_cast<uint32_t>(event->keysym.sym);
    kbd->current.key_state = event->repeat ? KBD_KEY_REPEATED : KBD_KEY_PRESSED;
    kbd->current.codepoint = ctrl_to_lv_key(event->keysym.sym);

    SDL_Keymod mods = SDL_GetModState();
    if (mods & KMOD_SHIFT) kbd->current.mods |= KBD_MOD_SHIFT;
    if (mods & KMOD_CTRL) kbd->current.mods |= KBD_MOD_CTRL;
    if (mods & KMOD_ALT) kbd->current.mods |= KBD_MOD_ALT;
    if (mods & KMOD_GUI) kbd->current.mods |= KBD_MOD_LOGO;

    const char *name = SDL_GetKeyName(event->keysym.sym);
    if (name) std::snprintf(kbd->current.sym_name, sizeof(kbd->current.sym_name), "%s", name);

    const char *seq = ctrl_utf8(event->keysym.sym);
    if (seq) std::snprintf(kbd->current.utf8, sizeof(kbd->current.utf8), "%s", seq);

    kbd->current_valid = true;
}

bool ctrl_letter(const SDL_KeyboardEvent *event, char *out)
{
    SDL_Keymod mods = SDL_GetModState();
    SDL_Keycode sym = event->keysym.sym;
    if ((mods & KMOD_CTRL) == 0 || sym < SDLK_a || sym > SDLK_z) return false;
    *out = static_cast<char>(sym - SDLK_a + 1);
    return true;
}

void set_text_key(WebKeyboard *kbd, const char *utf8)
{
    if (!kbd->current_valid) {
        std::memset(&kbd->current, 0, sizeof(kbd->current));
        kbd->current.key_state = KBD_KEY_PRESSED;
        kbd->current_valid = true;
    }
    std::snprintf(kbd->current.utf8, sizeof(kbd->current.utf8), "%s", utf8 ? utf8 : "");
    kbd->current.codepoint = utf8_first_codepoint(kbd->current.utf8);
}

void enqueue_key(const key_item *event)
{
    key_item *elm = static_cast<key_item *>(std::malloc(sizeof(*elm)));
    if (!elm) return;
    *elm = *event;
    elm->flage = 0;

    if (elm->key_code == KEY_ESC) LVGL_HOME_KEY_FLAG = elm->key_state;

    if (LVGL_RUN_FLAGE) {
        pthread_mutex_lock(&keyboard_mutex);
        STAILQ_INSERT_TAIL(&keyboard_queue, elm, entries);
        pthread_mutex_unlock(&keyboard_mutex);
    } else {
        std::free(elm);
    }
}

bool queue_has_data()
{
    pthread_mutex_lock(&keyboard_mutex);
    bool has_data = !STAILQ_EMPTY(&keyboard_queue);
    pthread_mutex_unlock(&keyboard_mutex);
    return has_data;
}

void web_keyboard_read(lv_indev_t *, lv_indev_data_t *data)
{
    data->key = 0;
    data->state = LV_INDEV_STATE_RELEASED;
    data->continue_reading = false;

    pthread_mutex_lock(&keyboard_mutex);
    if (!STAILQ_EMPTY(&keyboard_queue)) {
        const int intercept = cp0_keyboard_get_lvgl_keypad_intercept();
        key_item *elm = STAILQ_FIRST(&keyboard_queue);
        STAILQ_REMOVE_HEAD(&keyboard_queue, entries);

        lv_obj_t *root = lv_screen_active();
        if (root) lv_obj_send_event(root, static_cast<lv_event_code_t>(LV_EVENT_KEYBOARD), elm);

        if (!intercept) {
            data->key = linux_key_to_lv(elm->key_code);
            if (data->key)
                data->state = static_cast<lv_indev_state_t>(elm->key_state);
        }
        data->continue_reading = !STAILQ_EMPTY(&keyboard_queue);
        std::free(elm);
    }
    pthread_mutex_unlock(&keyboard_mutex);
}

void web_keyboard_delete_cb(lv_event_t *event)
{
    lv_indev_t *indev = static_cast<lv_indev_t *>(lv_event_get_user_data(event));
    auto *kbd = static_cast<WebKeyboard *>(lv_indev_get_driver_data(indev));
    lv_indev_set_driver_data(indev, nullptr);
    lv_indev_set_read_cb(indev, nullptr);
    delete kbd;
}

lv_indev_t *find_web_keyboard_indev()
{
    lv_indev_t *indev = lv_indev_get_next(nullptr);
    while (indev) {
        if (lv_indev_get_read_cb(indev) == web_keyboard_read) return indev;
        indev = lv_indev_get_next(indev);
    }
    return nullptr;
}

void copy_cstr(char *dst, size_t dst_size, const char *src)
{
    if (!dst || dst_size == 0) return;
    std::snprintf(dst, dst_size, "%s", src ? src : "");
}

std::string web_resource_path(const std::string &file)
{
    if (file.empty()) return "";
    if (file == "applications") return "/applications";
    if (file == "lock_file") return "/tmp/M5CardputerZero-APPLaunch.lock";
    if (file == "keyboard_device" || file == "keyboard_map") return "";
    if (file.size() > 0 && file[0] == '/') return file;

    auto has_ext = [&](const char *ext) {
        return file.size() >= std::strlen(ext) &&
               file.compare(file.size() - std::strlen(ext), std::strlen(ext), ext) == 0;
    };
    if (has_ext(".png") || has_ext(".jpg") || has_ext(".jpeg") || has_ext(".gif") || has_ext(".svg"))
        return "/images/" + file;
    if (has_ext(".wav") || has_ext(".mp3") || has_ext(".ogg"))
        return "/audio/" + file;
    if (has_ext(".ttf") || has_ext(".otf"))
        return "/font/" + file;
    return file;
}

} // namespace

std::string cp0_file_path(std::string file)
{
    return web_resource_path(file);
}

extern "C" {

__attribute__((weak)) void ui_global_hint_on_key(const key_item *) {}

void keyboard_pause(void) {}
void keyboard_resume(void) {}
void *keyboard_read_thread(void *) { return nullptr; }
void kbd_dump_keymap_table(void) {}

const char *kbd_state_name(int state)
{
    switch (state) {
    case KBD_KEY_RELEASED: return "UP";
    case KBD_KEY_PRESSED: return "DOWN";
    case KBD_KEY_REPEATED: return "REPEAT";
    default: return "???";
    }
}

lv_indev_t *lv_sdl_keyboard_create(void)
{
    STAILQ_INIT(&keyboard_queue);
    if (LV_EVENT_KEYBOARD == 0) LV_EVENT_KEYBOARD = lv_event_register_id();

    lv_indev_t *indev = lv_indev_create();
    if (!indev) return nullptr;
    lv_indev_set_type(indev, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(indev, web_keyboard_read);
    lv_indev_set_driver_data(indev, new WebKeyboard());
    lv_indev_set_mode(indev, LV_INDEV_MODE_EVENT);
    lv_indev_add_event_cb(indev, web_keyboard_delete_cb, LV_EVENT_DELETE, indev);
    return indev;
}

void lv_sdl_keyboard_handler(SDL_Event *event)
{
    if (!event) return;
    lv_indev_t *indev = find_web_keyboard_indev();
    if (!indev) return;

    auto *kbd = static_cast<WebKeyboard *>(lv_indev_get_driver_data(indev));
    if (!kbd) return;

    if (event->type == SDL_KEYDOWN) {
        fill_key_meta(kbd, &event->key);
        uint32_t ctrl_key = ctrl_to_lv_key(event->key.keysym.sym);
        char ctrl_char = 0;
        if (ctrl_key != 0 || ctrl_letter(&event->key, &ctrl_char)) {
            if (ctrl_char != 0) {
                kbd->current.utf8[0] = ctrl_char;
                kbd->current.utf8[1] = '\0';
                kbd->current.codepoint = static_cast<uint32_t>(ctrl_char);
            }
            enqueue_key(&kbd->current);
            if (event->key.keysym.scancode < SDL_NUM_SCANCODES) {
                kbd->active_keys[event->key.keysym.scancode] = kbd->current;
                kbd->active_valid[event->key.keysym.scancode] = true;
            }
            kbd->current_valid = false;
        }
    } else if (event->type == SDL_TEXTINPUT) {
        set_text_key(kbd, event->text.text);
        enqueue_key(&kbd->current);
        if (kbd->current_scancode < SDL_NUM_SCANCODES) {
            kbd->active_keys[kbd->current_scancode] = kbd->current;
            kbd->active_valid[kbd->current_scancode] = true;
        }
        kbd->current_valid = false;
    } else if (event->type == SDL_KEYUP) {
        key_item item{};
        SDL_Scancode scancode = event->key.keysym.scancode;
        if (scancode < SDL_NUM_SCANCODES && kbd->active_valid[scancode]) {
            item = kbd->active_keys[scancode];
            kbd->active_valid[scancode] = false;
        } else {
            fill_key_meta(kbd, &event->key);
            item = kbd->current;
        }
        item.key_state = KBD_KEY_RELEASED;
        enqueue_key(&item);
        if (kbd->current_scancode == scancode) kbd->current_valid = false;
    } else {
        return;
    }

    while (queue_has_data()) lv_indev_read(indev);
}

const char *cp0_file_path_c(const char *file)
{
    static std::string path;
    path = cp0_file_path(file ? file : "");
    return path.c_str();
}

int cp0_dir_list(const char *, cp0_dirent_t *, int, int *out_count)
{
    if (out_count) *out_count = 0;
    return 0;
}

cp0_watcher_t cp0_dir_watch_start(const char *) { return nullptr; }
int cp0_dir_watch_poll(cp0_watcher_t) { return 0; }
void cp0_dir_watch_stop(cp0_watcher_t) {}

int cp0_network_list(cp0_netif_info_t *entries, int max_entries, int *out_count)
{
    if (out_count) *out_count = 0;
    if (!entries || max_entries <= 0) return 0;
    copy_cstr(entries[0].iface, sizeof(entries[0].iface), "web");
    copy_cstr(entries[0].ipv4, sizeof(entries[0].ipv4), "0.0.0.0");
    copy_cstr(entries[0].netmask, sizeof(entries[0].netmask), "0.0.0.0");
    entries[0].is_up = 1;
    if (out_count) *out_count = 1;
    return 0;
}

int cp0_process_exec_blocking(const char *, volatile int *, int) { return -1; }
cp0_pid_t cp0_process_spawn(const char *, int) { return -1; }
void cp0_process_stop(cp0_pid_t) {}
int cp0_process_check_lock(const char *, int *holder_pid)
{
    if (holder_pid) *holder_pid = 0;
    return 0;
}
void cp0_process_kill(int, int) {}
void cp0_system_shutdown(void) {}
void cp0_system_reboot(void) {}

int cp0_process_run_argv(const char *const *, int) { return -1; }
int cp0_process_capture_argv(const char *const *, char *out, int out_size)
{
    if (out && out_size > 0) out[0] = '\0';
    return -1;
}
int cp0_file_read_first_line(const char *, char *out, int out_size)
{
    if (out && out_size > 0) out[0] = '\0';
    return -1;
}
int cp0_desktop_exec_is_safe(const char *, char *reason, int reason_size)
{
    copy_cstr(reason, static_cast<size_t>(std::max(reason_size, 0)), "web emulator does not execute desktop apps");
    return 0;
}

int cp0_network_default_info_read(cp0_eth_info_t *info)
{
    if (!info) return -1;
    std::memset(info, 0, sizeof(*info));
    copy_cstr(info->ipv4, sizeof(info->ipv4), "0.0.0.0");
    copy_cstr(info->gateway, sizeof(info->gateway), "0.0.0.0");
    copy_cstr(info->mac, sizeof(info->mac), "N/A");
    return 0;
}
int cp0_eth_info_read(cp0_eth_info_t *info) { return cp0_network_default_info_read(info); }
int cp0_account_info_read(cp0_account_info_t *info)
{
    if (!info) return -1;
    std::memset(info, 0, sizeof(*info));
    copy_cstr(info->user, sizeof(info->user), "web");
    copy_cstr(info->hostname, sizeof(info->hostname), "cardputer-zero-emu");
    return 0;
}

int cp0_system_apt_update_background(void) { return -1; }
int cp0_system_update_launcher_background(void) { return -1; }
int cp0_time_set(const char *) { return -1; }
int cp0_time_ntp_get(void) { return 0; }
int cp0_time_ntp_set(int) { return -1; }
int cp0_bq27220_calibrate(int) { return -1; }
int cp0_compass_calibrate(void) { return -1; }
int cp0_compass_read(cp0_compass_read_cb_t callback, void *user)
{
    cp0_compass_info_t info{};
    copy_cstr(info.status, sizeof(info.status), "Web emulator");
    if (callback) callback(0, &info, user);
    return 0;
}

cp0_battery_info_t cp0_battery_read(void)
{
    cp0_battery_info_t info{};
    info.voltage_mv = 3700;
    info.soc = 100;
    info.valid = 1;
    return info;
}

int cp0_backlight_read(void) { return 100; }
int cp0_backlight_max(void) { return 100; }
int cp0_backlight_write(int val) { return val; }

void cp0_time_str(char *buf, int buf_size)
{
    if (!buf || buf_size <= 0) return;
    std::time_t now = std::time(nullptr);
    std::tm *t = std::localtime(&now);
    if (t) std::snprintf(buf, static_cast<size_t>(buf_size), "%02d:%02d", t->tm_hour, t->tm_min);
    else buf[0] = '\0';
}

} // extern "C"
