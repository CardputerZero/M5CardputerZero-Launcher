#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CP0_WIFI_AP_MAX 32
#define CP0_WIFI_SSID_MAX 64
#define CP0_BT_DEVICE_MAX 16
#define CP0_BT_NAME_MAX 64

typedef struct {
    int voltage_mv;
    int current_ma;
    int temperature_c10;
    int soc;
    int remain_mah;
    int full_mah;
    int flags;
    int avg_current_ma;
    int valid;
} cp0_battery_info_t;

typedef struct {
    char ssid[CP0_WIFI_SSID_MAX];
    int signal; /* Wi-Fi RSSI in dBm; legacy backends may report 0..100 strength */
    char security[32];
    int in_use;
    int saved;
} cp0_wifi_ap_t;

typedef struct {
    int connected;
    char ssid[CP0_WIFI_SSID_MAX];
    char ip[48];
    int signal; /* Wi-Fi RSSI in dBm; legacy backends may report 0..100 strength */
    int ethernet; /* 1 if a wired ethernet device is connected */
} cp0_wifi_status_t;

typedef struct {
    int powered;
    int discoverable;
    char address[24];
    char alias[CP0_BT_NAME_MAX];
} cp0_bt_status_t;

typedef struct {
    char name[CP0_BT_NAME_MAX];
    char address[24];
    int rssi;
    int connected;
    int paired;
    int trusted;
} cp0_bt_device_t;

typedef struct {
    char iface[32];
    char ipv4[16];
    char netmask[16];
    int is_up;
} cp0_netif_info_t;

typedef struct {
    char name[256];
    int is_dir;
} cp0_dirent_t;

typedef struct {
    char ipv4[48];
    char gateway[48];
    char mac[32];
} cp0_eth_info_t;

typedef struct {
    char user[64];
    char hostname[128];
} cp0_account_info_t;

typedef struct {
    int initialized;
    int hw_ready;
    int tx_mode;
    int tx_in_progress;
    int has_sent_message;
    int rx_event;
    int tx_event;
    char spi_device[64];
    char last_rx[128];
    char last_tx[128];
    char diag[256];
    char probe_summary[256];
    char probe_display[128];
    char pi4io_status[160];
    float rssi;
    float snr;
} cp0_lora_info_t;

/* Request cancellation of an in-flight LoRa initialization. */
void cp0_lora_request_stop(void);
void cp0_lora_clear_stop(void);

typedef void *cp0_watcher_t;
typedef int cp0_pid_t;
typedef enum {
    CP0_SUDO_RESULT_SUCCESS = 0,
    CP0_SUDO_RESULT_AUTH_FAILED,
    CP0_SUDO_RESULT_EXEC_FAILED,
    CP0_SUDO_RESULT_CANCELLED,
    CP0_SUDO_RESULT_TIMED_OUT,
} cp0_sudo_result_t;

typedef enum {
    CP0_SUDO_CALLBACK_LVGL = 0,
    CP0_SUDO_CALLBACK_WORKER,
} cp0_sudo_callback_thread_t;

/* Output is delivered in order before completion. LVGL delivery uses bounded
 * backpressure, so callbacks should return promptly to avoid stalling the command. */
typedef void (*cp0_sudo_output_cb_t)(const char *data, size_t size, void *user);
typedef void (*cp0_sudo_complete_cb_t)(cp0_sudo_result_t result, int exit_code, void *user);

const char *cp0_file_path_c(const char *file);

int cp0_dir_list(const char *path, cp0_dirent_t *entries, int max_entries, int *out_count);
cp0_watcher_t cp0_dir_watch_start(const char *path);
int cp0_dir_watch_poll(cp0_watcher_t watcher);
void cp0_dir_watch_stop(cp0_watcher_t watcher);

int cp0_network_list(cp0_netif_info_t *entries, int max_entries, int *out_count);
typedef enum {
    CP0_WIFI_ERROR_INVALID = -1,
    CP0_WIFI_ERROR_RADIO_OFF = -2,
    CP0_WIFI_ERROR_AUTH = -3,
    CP0_WIFI_ERROR_NOT_FOUND = -4,
    CP0_WIFI_ERROR_IP_CONFIG = -5,
    CP0_WIFI_ERROR_SERVICE = -6,
    CP0_WIFI_ERROR_TIMEOUT = -7,
} cp0_wifi_error_t;
int cp0_wifi_status_read(cp0_wifi_status_t *status);
int cp0_wifi_scan(cp0_wifi_ap_t *entries, int max_entries);
int cp0_wifi_connect(const char *ssid, const char *password);
int cp0_wifi_connect_hidden(const char *ssid, const char *password);
int cp0_wifi_profile_forget(const char *ssid);
int cp0_wifi_profile_exists(const char *ssid);
int cp0_wifi_disconnect_active(void);
int cp0_wifi_radio_enabled(void);
int cp0_wifi_radio_set_enabled(int enabled);

int cp0_process_exec_blocking(const char *exec_path, int keep_root);
cp0_pid_t cp0_process_spawn(const char *exec_path, int keep_root);
void cp0_process_stop(cp0_pid_t pid);
int cp0_process_check_lock(const char *lock_path, int *holder_pid);
void cp0_process_kill(int pid, int grace_ms);
void cp0_system_shutdown(void);
void cp0_system_reboot(void);

int cp0_process_run_argv(const char *const *argv, int background);
int cp0_process_capture_argv(const char *const *argv, char *out, int out_size);
#if defined(__GNUC__) || defined(__clang__)
__attribute__((deprecated("use cp0_sudo_run_argv_async or cp0_sudo_run_shell_async")))
#endif
int cp0_process_run_sudo(const char *password, const char *const *argv);
int cp0_sudo_run_argv_async(const char *const *argv,
                            cp0_sudo_callback_thread_t callback_thread,
                            cp0_sudo_output_cb_t output_cb,
                            cp0_sudo_complete_cb_t complete_cb,
                            void *user);
int cp0_sudo_run_shell_async(const char *command,
                             cp0_sudo_callback_thread_t callback_thread,
                             cp0_sudo_output_cb_t output_cb,
                             cp0_sudo_complete_cb_t complete_cb,
                             void *user);
int cp0_sudo_run_argv_async_ex(const char *const *argv,
                               cp0_sudo_callback_thread_t callback_thread,
                               cp0_sudo_output_cb_t output_cb,
                               cp0_sudo_complete_cb_t complete_cb,
                               void *user,
                               /* Includes time spent waiting at the password prompt. */
                               int auth_timeout_ms,
                               /* Starts when the privileged command is launched. */
                               int exec_timeout_ms,
                               uint64_t *request_id);
/* Cancellation is idempotent while a request ID remains in the bounded recent
 * completion history. Returns -ENOENT only for an unknown or expired ID. */
int cp0_sudo_cancel(uint64_t request_id);
int cp0_sudo_queue_password(const char *password);
int cp0_file_read_first_line(const char *path, char *out, int out_size);
int cp0_desktop_exec_is_safe(const char *exec, char *reason, int reason_size);
int cp0_network_default_info_read(cp0_eth_info_t *info);
int cp0_eth_info_read(cp0_eth_info_t *info);
int cp0_account_info_read(cp0_account_info_t *info);
int cp0_system_apt_update_background(void);
int cp0_system_update_launcher_background(void);
int cp0_time_set(const char *timestamp);
/* NTP auto time-sync control. get returns 1 when enabled, 0 when disabled,
 * negative on error. Manual RTC time can only persist while NTP is disabled. */
int cp0_time_ntp_get(void);
int cp0_time_ntp_set(int enable);
int cp0_bq27220_calibrate(int command_index);
cp0_battery_info_t cp0_battery_read(void);
int cp0_backlight_read(void);
int cp0_backlight_max(void);
int cp0_backlight_write(int val);
void cp0_time_str(char *buf, int buf_size);

#ifdef __cplusplus
}
#else
#define cp0_file_path(file) cp0_file_path_c(file)
#endif
