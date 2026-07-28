#ifndef CP0_LVGL_KEYBOARD_INPUT_H
#define CP0_LVGL_KEYBOARD_INPUT_H

#include <pthread.h>
#include <stdint.h>
#include <sys/queue.h>
#ifdef __cplusplus
extern "C" {
#endif /* CP0_LVGL_KEYBOARD_INPUT_H */

// modifier bitmask
#define KBD_MOD_SHIFT  (1u << 0)
#define KBD_MOD_CTRL   (1u << 1)
#define KBD_MOD_ALT    (1u << 2)
#define KBD_MOD_LOGO   (1u << 3)
#define KBD_MOD_CAPS   (1u << 4)
#define KBD_MOD_NUM    (1u << 5)
#define KBD_MOD_FN     (1u << 6)

typedef enum {
    KBD_INPUT_CONTEXT_NAVIGATION = 0,
    KBD_INPUT_CONTEXT_TEXT = 1,
    KBD_INPUT_CONTEXT_GAME = 2,
} cp0_keyboard_input_context_t;

// key state
#define KBD_KEY_RELEASED  0
#define KBD_KEY_PRESSED   1
#define KBD_KEY_REPEATED  2

struct key_item {
    uint32_t key_code;      // Linux evdev key code
    uint32_t keysym;        // primary XKB keysym (xkb_keysym_t)
    uint32_t codepoint;     // corresponding Unicode code point, or 0 if none
    uint32_t mods;          // modifier bitmask (KBD_MOD_*)
    int      key_state;     // 0=released, 1=pressed, 2=repeat
    char     sym_name[65];  // XKB keysym name
    char     utf8[16];      // UTF-8 character (supports multi-byte compose output)
    char     flage;         // whether free is required
    uint32_t semantic_key;  // context-normalized KEY_* value
    cp0_keyboard_input_context_t input_context;
    STAILQ_ENTRY(key_item) entries;
};

typedef void (*cp0_keyboard_key_handler_t)(const struct key_item *item);

STAILQ_HEAD(keyboard_queue_t, key_item);
extern struct keyboard_queue_t keyboard_queue;
extern pthread_mutex_t keyboard_mutex;
extern volatile int LVGL_RUN_FLAGE;
extern volatile uint32_t LV_EVENT_KEYBOARD;

void *keyboard_read_thread(void *argv);
int cp0_keyboard_inject(uint32_t key_code, int key_state, uint32_t mods);
int cp0_keyboard_inject_text(const char *utf8);
void cp0_keyboard_set_global_key_handler(cp0_keyboard_key_handler_t handler);
/* Keep LV_EVENT_KEYBOARD delivery while suppressing the LVGL keypad group path. */
void cp0_keyboard_set_lvgl_keypad_intercept(int intercept);
int cp0_keyboard_get_lvgl_keypad_intercept(void);
void cp0_keyboard_set_input_context(cp0_keyboard_input_context_t context);
cp0_keyboard_input_context_t cp0_keyboard_get_input_context(void);
const char *kbd_state_name(int state);
void kbd_dump_keymap_table(void);
#ifdef __cplusplus
}
#endif
#endif
