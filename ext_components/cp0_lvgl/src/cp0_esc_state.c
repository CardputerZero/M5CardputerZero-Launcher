#include "cp0_esc_state.h"

#include <stdatomic.h>

static _Atomic int esc_key_state;

int cp0_esc_state_read(void)
{
    return atomic_load_explicit(&esc_key_state, memory_order_acquire);
}

void cp0_esc_state_write(int key_state)
{
    atomic_store_explicit(&esc_key_state, key_state, memory_order_release);
}

void cp0_esc_state_reset(void)
{
    cp0_esc_state_write(0);
}
