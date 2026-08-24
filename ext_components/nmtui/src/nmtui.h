#ifndef NMTUI_H_INCLUDED
#define NMTUI_H_INCLUDED

#ifdef __cplusplus
extern "C" {
#endif

#define NMTUI_VERSION "0.1.0"

int nmtui_wifi_init(void);
void nmtui_wifi_deinit(void);

int nmtui_wifi_is_available(void);

const char *nmtui_wifi_last_error(void);

#ifdef __cplusplus
}
#endif

#endif
