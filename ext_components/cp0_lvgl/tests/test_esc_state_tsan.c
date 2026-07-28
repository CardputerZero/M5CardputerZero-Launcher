#include "cp0_esc_state.h"

#include <assert.h>
#include <pthread.h>

enum { ITERATIONS = 100000 };

static void *write_esc_state(void *unused)
{
    (void)unused;
    for (int i = 0; i < ITERATIONS; ++i) cp0_esc_state_write(i & 1);
    return NULL;
}

int main(void)
{
    pthread_t writer;
    assert(pthread_create(&writer, NULL, write_esc_state, NULL) == 0);
    for (int i = 0; i < ITERATIONS; ++i) {
        const int state = cp0_esc_state_read();
        assert(state == 0 || state == 1);
    }
    assert(pthread_join(writer, NULL) == 0);
    cp0_esc_state_reset();
    assert(cp0_esc_state_read() == 0);
    return 0;
}
