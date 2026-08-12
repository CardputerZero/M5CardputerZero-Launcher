#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#ifdef __linux__
#include <poll.h>
#include <locale.h>
#endif
#include <stdbool.h>
#include <stdatomic.h>
#include <stdint.h>
#include <time.h>
#ifdef __linux__
#include <sys/ioctl.h>
#include <sys/timerfd.h>
#include <linux/input.h>
#include <libinput.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-compose.h>
#else
#include "input_keys.h"
#endif
#include "cp0_lvgl_app.h"
#include "cp0_keyboard_keymap.h"
#include "cp0_keyboard_navigation_contract.h"
#include "cp0_keyboard_lvgl_input.h"
#include "../cp0_keyboard_thread_lifecycle.h"
#include "../cp0_keyboard_queue.h"
#include "cp0_esc_state.h"
#include "../cp0_keyboard_text.h"
#include "keyboard_input.h"
#include "lvgl/lvgl.h"
#include "../../../../SDK/components/utilities/include/sample_log.h"

#undef SLOGD
#define SLOGD(...) do { } while (0)

/* ============================================================
 *  Global queue
 * ============================================================ */
struct keyboard_queue_t keyboard_queue;
pthread_mutex_t keyboard_mutex = PTHREAD_MUTEX_INITIALIZER;
volatile int LVGL_RUN_FLAGE = 1;
volatile uint32_t LV_EVENT_KEYBOARD;

static volatile int keyboard_paused_flag = 0;
static atomic_bool keyboard_shutdown_requested = false;
static pthread_t keyboard_read_thread_id;
static cp0_keyboard_thread_lifecycle_t keyboard_thread_lifecycle = {0};
static pthread_mutex_t keyboard_thread_mutex = PTHREAD_MUTEX_INITIALIZER;
#ifdef __linux__
static struct libinput *g_libinput = NULL;
void keyboard_pause(void) {
    keyboard_paused_flag = 1;
    if (g_libinput) libinput_suspend(g_libinput);
    SLOGI("[KBD] keyboard_pause()");
}
void keyboard_resume(void) {
    if (g_libinput) libinput_resume(g_libinput);
    keyboard_paused_flag = 0;
    SLOGI("[KBD] keyboard_resume()");
}
#else
void keyboard_pause(void) { keyboard_paused_flag = 1; }
void keyboard_resume(void) { keyboard_paused_flag = 0; }
#endif

/* ============================================================
 *  Debug: key_state name
 * ============================================================ */
const char *kbd_state_name(int state)
{
    switch (state) {
    case 0:  return "UP";
    case 1:  return "DOWN";
    case 2:  return "REPEAT";
    default: return "???";
    }
}

/* ============================================================
 *  Debug: dump all ASCII printable + letter + digit mappings once
 *  Call on startup so we can see how each key_code maps to utf8.
 * ============================================================ */
#ifdef __linux__
void kbd_dump_keymap_table(void)
{
    /* Linux evdev KEY_* values we care about: 2..53 covers 1..0 qwerty zxcvbnm */
    static const struct { uint32_t code; const char *name; } keys[] = {
        {KEY_1,"1"},{KEY_2,"2"},{KEY_3,"3"},{KEY_4,"4"},{KEY_5,"5"},
        {KEY_6,"6"},{KEY_7,"7"},{KEY_8,"8"},{KEY_9,"9"},{KEY_0,"0"},
        {KEY_Q,"q"},{KEY_W,"w"},{KEY_E,"e"},{KEY_R,"r"},{KEY_T,"t"},
        {KEY_Y,"y"},{KEY_U,"u"},{KEY_I,"i"},{KEY_O,"o"},{KEY_P,"p"},
        {KEY_A,"a"},{KEY_S,"s"},{KEY_D,"d"},{KEY_F,"f"},{KEY_G,"g"},
        {KEY_H,"h"},{KEY_J,"j"},{KEY_K,"k"},{KEY_L,"l"},
        {KEY_Z,"z"},{KEY_X,"x"},{KEY_C,"c"},{KEY_V,"v"},{KEY_B,"b"},
        {KEY_N,"n"},{KEY_M,"m"},
        {KEY_MINUS,"-"},{KEY_EQUAL,"="},{KEY_LEFTBRACE,"["},{KEY_RIGHTBRACE,"]"},
        {KEY_SEMICOLON,";"},{KEY_APOSTROPHE,"'"},{KEY_GRAVE,"`"},
        {KEY_BACKSLASH,"\\"},{KEY_COMMA,","},{KEY_DOT,"."},{KEY_SLASH,"/"},
        {KEY_SPACE,"SPACE"},{KEY_ENTER,"ENTER"},{KEY_ESC,"ESC"},
        {KEY_BACKSPACE,"BS"},{KEY_TAB,"TAB"},
        {KEY_UP,"UP"},{KEY_DOWN,"DOWN"},{KEY_LEFT,"LEFT"},{KEY_RIGHT,"RIGHT"},
        {KEY_HOME,"HOME"},{KEY_END,"END"},{KEY_DELETE,"DEL"},{KEY_INSERT,"INS"},
        {KEY_LEFTSHIFT,"LSHIFT"},{KEY_LEFTCTRL,"LCTRL"},{KEY_LEFTALT,"LALT"},
    };
    SLOGD("[KBD] ==== evdev key_code -> label table ====");
    for (size_t i = 0; i < sizeof(keys)/sizeof(keys[0]); i++) {
        SLOGD("[KBD]   code=%3u  %s", keys[i].code, keys[i].name);
    }
    SLOGD("[KBD] ==== end ====");
    fflush(stdout);
}
#else
void kbd_dump_keymap_table(void) {}
#endif

static const char *getenv_default(const char *name, const char *dflt)
{
    const char *value = getenv(name);
    return (value && value[0] != '\0') ? value : dflt;
}

/* ============================================================
 *  Parameters
 * ============================================================ */
#define EVDEV_KEYCODE_OFFSET   8
#define REPEAT_RATE_MS        50   /* interval between subsequent repeats */

static atomic_int keyboard_input_context = KBD_INPUT_CONTEXT_NAVIGATION;
static atomic_uint keyboard_input_context_generation = 1;

void cp0_keyboard_set_input_context(cp0_keyboard_input_context_t context)
{
    if (context < KBD_INPUT_CONTEXT_NAVIGATION || context > KBD_INPUT_CONTEXT_GAME)
        context = KBD_INPUT_CONTEXT_NAVIGATION;
    const int previous = atomic_exchange_explicit(
        &keyboard_input_context, context, memory_order_acq_rel);
    if (previous != context)
        atomic_fetch_add_explicit(
            &keyboard_input_context_generation, 1, memory_order_release);
}

cp0_keyboard_input_context_t cp0_keyboard_get_input_context(void)
{
    return (cp0_keyboard_input_context_t)atomic_load_explicit(
        &keyboard_input_context, memory_order_acquire);
}

/* ============================================================
 *  libinput open/close callbacks
 * ============================================================ */
static int open_restricted(const char *path, int flags, void *user_data) {
    (void)user_data;
    int fd = open(path, flags);
    if (fd < 0) {
        fprintf(stderr, "Failed to open %s: %s\n", path, strerror(errno));
        return -errno;
    }
    /* Keyboard grabbing is intentionally disabled so other programs can read
     * the same input device while APPLaunch is running.
     *
     * if (ioctl(fd, EVIOCGRAB, 1) < 0 && errno != EBUSY) {
     *     fprintf(stderr, "[KBD] EVIOCGRAB %s failed: %s\n", path, strerror(errno));
     * }
     */
    return fd;
}
static void close_restricted(int fd, void *user_data) { (void)user_data; close(fd); }
static const struct libinput_interface interface = {
    .open_restricted  = open_restricted,
    .close_restricted = close_restricted,
};

/* ============================================================
 *  Keyboard context
 * ============================================================ */
struct kbd_ctx {
    struct libinput        *li;
    struct libinput_device *dev;

    struct xkb_context        *ctx;
    struct xkb_keymap         *keymap;
    struct xkb_state          *state;
    struct xkb_compose_table  *compose_table;
    struct xkb_compose_state  *compose_state;

    /* key repeat */
    int      repeat_fd;
    bool     repeating;
    struct key_item repeat_template;   /* save the last pressed key for repeat copies */
    bool fn_pressed;
    uint64_t fn_pressed_at_ms;
    unsigned int input_context_generation;
};

static uint64_t monotonic_ms(void)
{
    struct timespec now = {0};
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 0;
    return (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u;
}

static void recover_stale_fn(struct kbd_ctx *kc)
{
    const unsigned int generation = atomic_load_explicit(
        &keyboard_input_context_generation, memory_order_acquire);
    const uint64_t now = monotonic_ms();
    if (kc->input_context_generation != generation ||
        (kc->fn_pressed && now - kc->fn_pressed_at_ms >= 10000u)) {
        kc->fn_pressed = false;
        kc->input_context_generation = generation;
    }
}

/* ============================================================
 *  xkbcommon log callback
 * ============================================================ */
static void uxkb_log(struct xkb_context *ctx, enum xkb_log_level level,
                     const char *fmt, va_list args)
{
    (void)ctx; (void)level;
    vfprintf(stderr, fmt, args);
}

/* ============================================================
 *  modifier bitmask
 * ============================================================ */
static uint32_t get_mods(struct xkb_state *state) {
    uint32_t m = 0;
    if (xkb_state_mod_name_is_active(state, XKB_MOD_NAME_SHIFT, XKB_STATE_MODS_EFFECTIVE) > 0)
        m |= KBD_MOD_SHIFT;
    if (xkb_state_mod_name_is_active(state, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE) > 0)
        m |= KBD_MOD_CTRL;
    if (xkb_state_mod_name_is_active(state, XKB_MOD_NAME_ALT, XKB_STATE_MODS_EFFECTIVE) > 0)
        m |= KBD_MOD_ALT;
    if (xkb_state_mod_name_is_active(state, XKB_MOD_NAME_LOGO, XKB_STATE_MODS_EFFECTIVE) > 0)
        m |= KBD_MOD_LOGO;
    if (xkb_state_mod_name_is_active(state, XKB_MOD_NAME_CAPS, XKB_STATE_MODS_EFFECTIVE) > 0)
        m |= KBD_MOD_CAPS;
    if (xkb_state_mod_name_is_active(state, XKB_MOD_NAME_NUM, XKB_STATE_MODS_EFFECTIVE) > 0)
        m |= KBD_MOD_NUM;
    return m;
}

/* ============================================================
 *  LED update (via libinput; no need to write evdev directly)
 * ============================================================ */
static void update_leds(struct kbd_ctx *kc) {
    enum libinput_led leds = 0;
    if (xkb_state_led_name_is_active(kc->state, XKB_LED_NAME_NUM) > 0)
        leds |= LIBINPUT_LED_NUM_LOCK;
    if (xkb_state_led_name_is_active(kc->state, XKB_LED_NAME_CAPS) > 0)
        leds |= LIBINPUT_LED_CAPS_LOCK;
    if (xkb_state_led_name_is_active(kc->state, XKB_LED_NAME_SCROLL) > 0)
        leds |= LIBINPUT_LED_SCROLL_LOCK;
    libinput_device_led_update(kc->dev, leds);
}

/* ============================================================
 *  Enqueue
 * ============================================================ */
static void enqueue_key(const struct key_item *src) {
    struct key_item item = *src;
    item.flage = 0;

    /* DEBUG: every raw key event from keyboard thread */
    char utf8_dbg[64] = "";
    int di = 0;
    for (int i = 0; i < (int)sizeof(item.utf8) && item.utf8[i] && di < 60; i++) {
        unsigned char c = (unsigned char)item.utf8[i];
        if (c >= 0x20 && c < 0x7f) utf8_dbg[di++] = (char)c;
        else di += snprintf(utf8_dbg+di, 64-di, "\\x%02x", c);
    }
    utf8_dbg[di] = '\0';
    SLOGD("[KBD] enqueue code=%u state=%s sym=%s utf8='%s' cp=0x%x mods=0x%x run=%d home_flag=%d",
          item.key_code, kbd_state_name(item.key_state), item.sym_name,
          utf8_dbg, item.codepoint, item.mods, LVGL_RUN_FLAGE, cp0_esc_state_read());
    (void)cp0_keyboard_queue_push(&item);
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
    const char *ctrl = cp0_keyboard_control_utf8(key_code);
    if (ctrl) snprintf(item.utf8, sizeof(item.utf8), "%s", ctrl);
    snprintf(item.sym_name, sizeof(item.sym_name), "RPC_%u", key_code);
    enqueue_key(&item);
    return 0;
}

int cp0_keyboard_inject_text(const char *utf8)
{
    if (!utf8 || cp0_keyboard_utf8_validate(utf8) != 0)
        return -1;

    const char *cursor = utf8;
    while (*cursor) {
        size_t length = 0;
        uint32_t codepoint = 0;
        if (cp0_keyboard_utf8_decode_one(cursor, &codepoint, &length) != 0) return -1;

        struct key_item item = {0};
        item.key_state = KBD_KEY_RELEASED;
        memcpy(item.utf8, cursor, length);
        item.codepoint = codepoint;
        snprintf(item.sym_name, sizeof(item.sym_name), "RPC_TEXT");
        enqueue_key(&item);
        cursor += length;
    }
    return 0;
}

/* ============================================================
 *  Key repeat control
 * ============================================================ */
static void repeat_start(struct kbd_ctx *kc) {
    const uint32_t key_code = kc->repeat_template.key_code;
    const uint32_t delay_ms = cp0_keyboard_repeat_delay_ms(
        key_code, kc->repeat_template.input_context);
    struct itimerspec ts = {
        .it_interval = { .tv_sec = 0, .tv_nsec = (long)REPEAT_RATE_MS  * 1000000L },
        .it_value    = { .tv_sec = 0, .tv_nsec = (long)delay_ms * 1000000L },
    };
    timerfd_settime(kc->repeat_fd, 0, &ts, NULL);
    kc->repeating = true;
}
static void repeat_stop(struct kbd_ctx *kc) {
    struct itimerspec ts = {0};
    timerfd_settime(kc->repeat_fd, 0, &ts, NULL);
    kc->repeating = false;
}

/* Encode a UTF-32 code point as UTF-8 and return the byte count */
/* ============================================================
 *  Core: handle one key event
 * ============================================================ */
static void process_key(struct kbd_ctx *kc, uint32_t code, int pressed)
{
    recover_stale_fn(kc);
    xkb_keycode_t keycode = code + EVDEV_KEYCODE_OFFSET;
    struct key_item item = {0};
    item.key_code  = code;
    item.key_state = pressed ? KBD_KEY_PRESSED : KBD_KEY_RELEASED;
    item.input_context = cp0_keyboard_get_input_context();
    item.semantic_key = cp0_keyboard_semantic_key(code, item.input_context);
    const bool fn_active = kc->fn_pressed || (code == KEY_FN && pressed);
    if (code == KEY_FN) {
        kc->fn_pressed = pressed;
        if (pressed) kc->fn_pressed_at_ms = monotonic_ms();
    }

    /* ---------- 1. TCA8418 custom keycodes first ---------- */
    const struct cp0_keyboard_keymap_entry *mapped = cp0_keyboard_keymap_lookup(code);
    if (mapped) {
        xkb_keysym_t sym = xkb_keysym_from_name(mapped->sym_name,
                                                XKB_KEYSYM_NO_FLAGS);
        snprintf(item.sym_name, sizeof(item.sym_name), "%s", mapped->sym_name);
        snprintf(item.utf8,     sizeof(item.utf8),     "%s", mapped->utf8);
        item.keysym    = sym;
        item.codepoint = (sym != XKB_KEY_NoSymbol) ? xkb_keysym_to_utf32(sym) : 0;
        item.mods      = get_mods(kc->state) | (fn_active ? KBD_MOD_FN : 0);

        /* repeat handling */
        if (pressed) {
            kc->repeat_template = item;
            kc->repeat_template.key_state = KBD_KEY_REPEATED;
            repeat_start(kc);
        } else if (kc->repeating && kc->repeat_template.key_code == code) {
            repeat_stop(kc);
        }
        enqueue_key(&item);
        return;
    }

    /* ---------- 2. standard xkbcommon flow ---------- */
    const xkb_keysym_t *syms;
    int num = xkb_state_key_get_syms(kc->state, keycode, &syms);
    xkb_keysym_t one_sym = XKB_KEY_NoSymbol;

    if (num == 1) {
        /* handle Lock modifiers (following uterm + libxkbcommon recommendations) */
        one_sym = xkb_state_key_get_one_sym(kc->state, keycode);
    } else if (num > 1) {
        one_sym = syms[0];
    }

    /* ---------- 3. Compose handling (dead keys, etc.) ---------- */
    enum xkb_compose_status cstatus = XKB_COMPOSE_NOTHING;
    bool compose_produced_utf8 = false;

    if (kc->compose_state && pressed) {
        xkb_compose_state_feed(kc->compose_state, one_sym);
        cstatus = xkb_compose_state_get_status(kc->compose_state);

        if (cstatus == XKB_COMPOSE_COMPOSED) {
            xkb_keysym_t csym = xkb_compose_state_get_one_sym(kc->compose_state);
            if (csym != XKB_KEY_NoSymbol) {
                one_sym = csym;
            }
            /* get the composed UTF-8 string */
            int n = xkb_compose_state_get_utf8(kc->compose_state,
                                               item.utf8, sizeof(item.utf8));
            if (n > 0) compose_produced_utf8 = true;

            /* If neither keysym nor utf8 is available, treat it as canceled */
            if (csym == XKB_KEY_NoSymbol && !compose_produced_utf8)
                cstatus = XKB_COMPOSE_CANCELLED;
        }
        if (cstatus == XKB_COMPOSE_COMPOSED || cstatus == XKB_COMPOSE_CANCELLED)
            xkb_compose_state_reset(kc->compose_state);
    }

    /* ---------- 4. update xkb state (must be after get_syms) ---------- */
    enum xkb_state_component changed = 0;
    if (pressed)
        changed = xkb_state_update_key(kc->state, keycode, XKB_KEY_DOWN);
    else
        changed = xkb_state_update_key(kc->state, keycode, XKB_KEY_UP);
    if (changed & XKB_STATE_LEDS)
        update_leds(kc);

    /* ---------- 5. filter events that are composing or canceled ---------- */
    if (cstatus == XKB_COMPOSE_COMPOSING || cstatus == XKB_COMPOSE_CANCELLED)
        return;
    if (num <= 0 && !compose_produced_utf8)
        return;

    /* ---------- 6. fill item ---------- */
    xkb_keysym_get_name(one_sym, item.sym_name, sizeof(item.sym_name));
    item.keysym    = one_sym;
    item.codepoint = (one_sym != XKB_KEY_NoSymbol)
                         ? xkb_keysym_to_utf32(one_sym) : 0;
    item.mods      = get_mods(kc->state) | (fn_active ? KBD_MOD_FN : 0);

    /* If compose did not provide utf8, get it from xkb_state */
    if (item.utf8[0] == '\0') {
        xkb_state_key_get_utf8(kc->state, keycode,
                               item.utf8, sizeof(item.utf8));
        if (item.utf8[0] == '\0' && item.codepoint != 0) {
            /* get_utf8 filters control characters; fall back to manual encoding */
            (void)cp0_keyboard_utf32_to_utf8(item.codepoint, item.utf8, sizeof(item.utf8));
        }
        if (item.codepoint == 0)
            item.codepoint = xkb_state_key_get_utf32(kc->state, keycode);
    }
    
    /* ---------- 6.5 control-key fallback mapping ---------- */
    /* xkbcommon does not produce utf8 for function keys (UP/DOWN/ENTER/BACKSPACE, etc.),
    * manually fill ANSI/VT100 terminal control characters here for upper layers */
    if (item.utf8[0] == '\0') {
        const char *ctrl = cp0_keyboard_control_utf8(code);
        if (ctrl) {
            snprintf(item.utf8, sizeof(item.utf8), "%s", ctrl);
        }
    }

    /* ---------- 7. repeat control ---------- */
    if (pressed && xkb_keymap_key_repeats(kc->keymap, keycode)) {
        kc->repeat_template = item;
        kc->repeat_template.key_state = KBD_KEY_REPEATED;
        repeat_start(kc);
    } else if (!pressed && kc->repeating &&
               kc->repeat_template.key_code == code) {
        repeat_stop(kc);
    }

    /* ---------- 8. Enqueue ---------- */
    enqueue_key(&item);
}

/* ============================================================
 *  xkb initialization (with rmlvo fallback + compose)
 * ============================================================ */
static int init_xkb(struct kbd_ctx *kc,
                    const char *layout, const char *locale)
{
    kc->ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!kc->ctx) { fprintf(stderr, "xkb_context_new failed\n"); return -1; }
    xkb_context_set_log_fn(kc->ctx, uxkb_log);

    struct xkb_rule_names rmlvo = {
        .rules   = "evdev",
        .model   = NULL,
        .layout  = layout ? layout : "us",
        .variant = NULL,
        .options = NULL,
    };
    kc->keymap = xkb_keymap_new_from_names(kc->ctx, &rmlvo,
                                           XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (!kc->keymap) {
        /* empty rmlvo fallback */
        struct xkb_rule_names empty = {0};
        kc->keymap = xkb_keymap_new_from_names(kc->ctx, &empty,
                                               XKB_KEYMAP_COMPILE_NO_FLAGS);
    }
    if (!kc->keymap) { fprintf(stderr, "failed to create keymap\n"); return -1; }

    kc->state = xkb_state_new(kc->keymap);
    if (!kc->state) { fprintf(stderr, "failed to create xkb_state\n"); return -1; }

    /* Compose table */
    if (!locale || !*locale) {
        locale = getenv("LC_ALL");
        if (!locale || !*locale) locale = getenv("LC_CTYPE");
        if (!locale || !*locale) locale = getenv("LANG");
        if (!locale || !*locale) locale = "C";
    }
    kc->compose_table = xkb_compose_table_new_from_locale(
        kc->ctx, locale, XKB_COMPOSE_COMPILE_NO_FLAGS);
    if (kc->compose_table) {
        kc->compose_state = xkb_compose_state_new(kc->compose_table,
                                                  XKB_COMPOSE_STATE_NO_FLAGS);
        if (!kc->compose_state)
            fprintf(stderr, "Warning: failed to create compose_state; disabling compose\n");
    } else {
        fprintf(stderr, "Warning: locale=%s has no compose table\n", locale);
    }
    return 0;
}

static void free_xkb(struct kbd_ctx *kc) {
    if (kc->compose_state) xkb_compose_state_unref(kc->compose_state);
    if (kc->compose_table) xkb_compose_table_unref(kc->compose_table);
    if (kc->state)  xkb_state_unref(kc->state);
    if (kc->keymap) xkb_keymap_unref(kc->keymap);
    if (kc->ctx)    xkb_context_unref(kc->ctx);
}

/* Optional: rebuild state on VT wakeup while preserving locked mods/layout (see uxkb_dev_wake_up) */
static void kbd_wake_up(struct kbd_ctx *kc) {
    xkb_mod_mask_t locked_mods = xkb_state_serialize_mods(kc->state,
                                                          XKB_STATE_MODS_LOCKED);
    xkb_layout_index_t locked_layout = xkb_state_serialize_layout(
        kc->state, XKB_STATE_LAYOUT_LOCKED);
    xkb_state_unref(kc->state);
    kc->state = xkb_state_new(kc->keymap);
    if (!kc->state) return;
    xkb_state_update_mask(kc->state, 0, 0, locked_mods, 0, 0, locked_layout);
    update_leds(kc);
    if (kc->compose_state) xkb_compose_state_reset(kc->compose_state);
}

/* ============================================================
 *  Thread main loop
 * ============================================================ */
void *keyboard_read_thread(void *argv) {
    char *device_path_arg = argv ? (char *)argv : NULL;
    const char *device_path = device_path_arg ? device_path_arg
        : "/dev/input/by-path/platform-3f804000.i2c-event";

    struct kbd_ctx kc = {0};
    kc.repeat_fd = -1;

    /* ---------- 1. libinput ---------- */
    kc.li = libinput_path_create_context(&interface, NULL);
    if (!kc.li) { fprintf(stderr, "failed to create libinput context\n"); goto out; }

    kc.dev = libinput_path_add_device(kc.li, device_path);
    if (!kc.dev) {
        fprintf(stderr, "Failed to add device %s (root permissions may be required)\n", device_path);
        goto out;
    }
    if (!libinput_device_has_capability(kc.dev, LIBINPUT_DEVICE_CAP_KEYBOARD)) {
        fprintf(stderr, "%s is not a keyboard device\n", device_path);
        goto out;
    }

    /* ---------- 2. xkbcommon ---------- */
    if (init_xkb(&kc, "us", NULL) < 0) goto out;

    /* ---------- 3. key repeat timerfd ---------- */
    kc.repeat_fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
    if (kc.repeat_fd < 0) { perror("timerfd_create"); goto out; }

    /* ---------- 4. event loop ---------- */
    int li_fd = libinput_get_fd(kc.li);
    struct pollfd pfds[2] = {
        { .fd = li_fd,        .events = POLLIN },
        { .fd = kc.repeat_fd, .events = POLLIN },
    };

    g_libinput = kc.li;
    SLOGI("Start listening for keyboard input (%s)", device_path);
    libinput_dispatch(kc.li);

    while (!atomic_load_explicit(&keyboard_shutdown_requested,
                                 memory_order_acquire)) {
        if (keyboard_paused_flag) {
            usleep(50000);
            continue;
        }
        int pr = poll(pfds, 2, 100);
        if (pr < 0) {
            if (errno == EINTR) continue;
            perror("poll"); break;
        }
        if (pr == 0) continue;

        /* keyboard event */
        if (pfds[0].revents & POLLIN) {
            libinput_dispatch(kc.li);
            struct libinput_event *ev;
            while ((ev = libinput_get_event(kc.li)) != NULL) {
                if (libinput_event_get_type(ev) == LIBINPUT_EVENT_KEYBOARD_KEY) {
                    struct libinput_event_keyboard *kev =
                        libinput_event_get_keyboard_event(ev);
                    uint32_t code = libinput_event_keyboard_get_key(kev);
                    enum libinput_key_state ks =
                        libinput_event_keyboard_get_key_state(kev);
                    process_key(&kc, code,
                                ks == LIBINPUT_KEY_STATE_PRESSED ? 1 : 0);
                }
                libinput_event_destroy(ev);
            }
        }

        /* repeat timer triggered */
        if (pfds[1].revents & POLLIN) {
            uint64_t exp;
            while (read(kc.repeat_fd, &exp, sizeof(exp)) == sizeof(exp)) {
                if (kc.repeating) {
                    recover_stale_fn(&kc);
                    /* refresh mods (prevents Shift, etc. from changing during repeat) */
                    kc.repeat_template.mods = get_mods(kc.state) |
                        (kc.fn_pressed ? KBD_MOD_FN : 0);
                    enqueue_key(&kc.repeat_template);
                }
            }
        }
    }

out:
    g_libinput = NULL;
    if (kc.repeat_fd >= 0) close(kc.repeat_fd);
    free_xkb(&kc);
    if (kc.dev) libinput_path_remove_device(kc.dev);
    if (kc.li) libinput_unref(kc.li);
    free(device_path_arg);
    return NULL;
}

void init_input(void)
{
    pthread_mutex_lock(&keyboard_thread_mutex);
    if (!cp0_keyboard_thread_can_init(&keyboard_thread_lifecycle)) {
        pthread_mutex_unlock(&keyboard_thread_mutex);
        return;
    }

    cp0_keyboard_queue_init();

    if (LV_EVENT_KEYBOARD == 0)
        LV_EVENT_KEYBOARD = lv_event_register_id();

    const char *default_keyboard_device = cp0_file_path("keyboard_device");
    const char *keyboard_device = getenv_default("LV_LINUX_KEYBOARD_DEVICE", default_keyboard_device);
    if (keyboard_device == NULL || keyboard_device[0] == '\0')
        keyboard_device = "/dev/input/by-path/platform-3f804000.i2c-event";
    setenv("APPLAUNCH_LINUX_KEYBOARD_DEVICE", keyboard_device, 1);
    setenv("LV_LINUX_KEYBOARD_DEVICE", keyboard_device, 1);

    cp0_keyboard_keymap_load();

    char *keyboard_device_arg = strdup(keyboard_device);
    if (keyboard_device_arg == NULL) {
        perror("strdup keyboard_device");
        pthread_mutex_unlock(&keyboard_thread_mutex);
        return;
    }

    atomic_store_explicit(&keyboard_shutdown_requested, false,
                          memory_order_release);
    if (pthread_create(&keyboard_read_thread_id, NULL, keyboard_read_thread, keyboard_device_arg) != 0) {
        perror("pthread_create keyboard_read_thread");
        free(keyboard_device_arg);
        pthread_mutex_unlock(&keyboard_thread_mutex);
        return;
    }

    cp0_keyboard_thread_mark_started(&keyboard_thread_lifecycle);
    cp0_keyboard_create_lvgl_input_devices();
    pthread_mutex_unlock(&keyboard_thread_mutex);
}

void deinit_input(void)
{
    pthread_t thread;
    int should_join = 0;
    atomic_store_explicit(&keyboard_shutdown_requested, true,
                          memory_order_release);
    pthread_mutex_lock(&keyboard_thread_mutex);
    should_join = cp0_keyboard_thread_begin_deinit(
        &keyboard_thread_lifecycle);
    if (should_join) thread = keyboard_read_thread_id;
    pthread_mutex_unlock(&keyboard_thread_mutex);
    if (!should_join) return;
    const int joined = !pthread_equal(thread, pthread_self()) &&
                       pthread_join(thread, NULL) == 0;
    pthread_mutex_lock(&keyboard_thread_mutex);
    if (joined)
        cp0_keyboard_thread_finish_deinit(&keyboard_thread_lifecycle);
    else
        cp0_keyboard_thread_cancel_deinit(&keyboard_thread_lifecycle);
    pthread_mutex_unlock(&keyboard_thread_mutex);
}
