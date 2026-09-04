#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

void init_freambuffer_disp();
void init_input();
void deinit_input(void);
void init_filesystem(void);
void init_audio();
#ifdef __cplusplus
void deinit_audio(void) noexcept;
#else
void deinit_audio(void);
#endif
void init_pty(void);
#ifdef __cplusplus
void deinit_pty(void) noexcept;
#else
void deinit_pty(void);
#endif
void init_config(void);
void init_process(void);
void init_screenshot(void);
void init_rpc(void);
#ifdef __cplusplus
void deinit_rpc(void) noexcept;
#else
void deinit_rpc(void);
#endif
void init_lora(void);
void init_wifi();
void init_bluetooth(void);
void init_settings(void);
void init_osinfo(void);
void init_bq27220(void);
void deinit_bq27220(void);
void init_battery();
void init_camera(void);
#ifdef __cplusplus
void deinit_camera(void) noexcept;
#else
void deinit_camera(void);
#endif
void init_soundcard(void);
#ifdef __cplusplus
}
#endif
