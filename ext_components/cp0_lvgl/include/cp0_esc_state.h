#pragma once

#ifdef __cplusplus
extern "C" {
#endif

int cp0_esc_state_read(void);
void cp0_esc_state_write(int key_state);
void cp0_esc_state_reset(void);

#ifdef __cplusplus
}
#endif
