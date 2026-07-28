#include "cp0_keyboard_queue.h"

#include "cp0_esc_state.h"
#include "input_keys.h"

#include <stdlib.h>

static pthread_once_t queue_once = PTHREAD_ONCE_INIT;

static void initialize_queue(void)
{
    STAILQ_INIT(&keyboard_queue);
}

void cp0_keyboard_queue_init(void)
{
    pthread_once(&queue_once, initialize_queue);
}

int cp0_keyboard_queue_push(const struct key_item *item)
{
    if (!item) return -1;
    cp0_keyboard_queue_init();
    struct key_item *copy = malloc(sizeof(*copy));
    if (!copy) return -1;
    *copy = *item;
    copy->flage = 0;
    if (copy->key_code == KEY_ESC) cp0_esc_state_write(copy->key_state);
    if (!LVGL_RUN_FLAGE) {
        free(copy);
        return 0;
    }
    pthread_mutex_lock(&keyboard_mutex);
    STAILQ_INSERT_TAIL(&keyboard_queue, copy, entries);
    pthread_mutex_unlock(&keyboard_mutex);
    return 0;
}

struct key_item *cp0_keyboard_queue_pop(void)
{
    cp0_keyboard_queue_init();
    pthread_mutex_lock(&keyboard_mutex);
    struct key_item *item = STAILQ_FIRST(&keyboard_queue);
    if (item) STAILQ_REMOVE_HEAD(&keyboard_queue, entries);
    pthread_mutex_unlock(&keyboard_mutex);
    return item;
}

int cp0_keyboard_queue_has_data(void)
{
    cp0_keyboard_queue_init();
    pthread_mutex_lock(&keyboard_mutex);
    const int result = !STAILQ_EMPTY(&keyboard_queue);
    pthread_mutex_unlock(&keyboard_mutex);
    return result;
}

void cp0_keyboard_queue_clear(void)
{
    struct key_item *item;
    while ((item = cp0_keyboard_queue_pop()) != NULL) free(item);
}
