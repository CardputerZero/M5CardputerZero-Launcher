/*
 * nmtui.h - libnm-backed cp0 Wi-Fi signal adapter
 */
#ifndef _NMTUI_H_
#define _NMTUI_H_

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

#endif /* _NMTUI_H_ */
