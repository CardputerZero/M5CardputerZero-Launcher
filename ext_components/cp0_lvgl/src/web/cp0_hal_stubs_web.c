#include "hal/hal_audio.h"
#include "hal/hal_filesystem.h"
#include "hal/hal_network.h"
#include "hal/hal_paths.h"
#include "hal/hal_process.h"
#include "hal/hal_settings.h"
#include "cp0_lvgl_app.h"
#include "keyboard_input.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static cp0_keyboard_key_handler_t global_key_handler;
static int lvgl_keypad_intercept;

void cp0_keyboard_set_global_key_handler(cp0_keyboard_key_handler_t handler)
{
    global_key_handler = handler;
}

void cp0_keyboard_set_lvgl_keypad_intercept(int intercept)
{ lvgl_keypad_intercept = intercept != 0; }

int cp0_keyboard_get_lvgl_keypad_intercept(void)
{ return lvgl_keypad_intercept; }

int cp0_process_run_sudo(const char *password, const char *const *argv)
{ (void)password; (void)argv; return -1; }

int cp0_sudo_run_argv_async(const char *const *argv,
                            cp0_sudo_callback_thread_t callback_thread,
                            cp0_sudo_output_cb_t output_cb,
                            cp0_sudo_complete_cb_t complete_cb,
                            void *user)
{
    (void)argv; (void)callback_thread; (void)output_cb; (void)complete_cb; (void)user;
    return -ENOTSUP;
}

int cp0_sudo_run_shell_async(const char *command,
                             cp0_sudo_callback_thread_t callback_thread,
                             cp0_sudo_output_cb_t output_cb,
                             cp0_sudo_complete_cb_t complete_cb,
                             void *user)
{
    (void)command; (void)callback_thread; (void)output_cb; (void)complete_cb; (void)user;
    return -ENOTSUP;
}

int cp0_sudo_run_argv_async_ex(const char *const *argv,
                               cp0_sudo_callback_thread_t callback_thread,
                               cp0_sudo_output_cb_t output_cb,
                               cp0_sudo_complete_cb_t complete_cb, void *user,
                               int auth_timeout_ms, int exec_timeout_ms,
                               uint64_t *request_id)
{
    (void)auth_timeout_ms; (void)exec_timeout_ms;
    if (request_id) *request_id = 0;
    return cp0_sudo_run_argv_async(argv, callback_thread, output_cb, complete_cb, user);
}

int cp0_sudo_cancel(uint64_t request_id)
{ (void)request_id; return -ENOTSUP; }

void cp0_lora_request_stop(void) {}
void cp0_lora_clear_stop(void) {}

void hal_audio_init(void) {}
void hal_audio_play(const char *path) { (void)path; }
void hal_audio_play_sync(const char *path) { (void)path; }
void hal_audio_stop(void) {}
void hal_audio_deinit(void) {}

int hal_dir_list(const char *path, hal_dirent_t *entries, int max_entries, int *out_count)
{ (void)path; (void)entries; (void)max_entries; *out_count = 0; return -1; }
hal_watcher_t hal_dir_watch_start(const char *path) { (void)path; return 0; }
int hal_dir_watch_poll(hal_watcher_t watcher) { (void)watcher; return 0; }
void hal_dir_watch_stop(hal_watcher_t watcher) { (void)watcher; }

int hal_network_list(hal_netif_info_t *entries, int max_entries, int *out_count)
{ (void)entries; (void)max_entries; *out_count = 0; return 0; }

void hal_paths_init(const char *exe_dir) { (void)exe_dir; }
const char *hal_path_data_dir(void) { return "/"; }
const char *hal_path_applications_dir(void) { return "/applications"; }
const char *hal_path_lock_file(void) { return "/tmp/lock"; }
const char *hal_path_font_dir(void) { return "/font"; }
const char *hal_path_font_regular(void) { return "/font/regular.ttf"; }
const char *hal_path_font_mono(void) { return "/font/mono.ttf"; }
const char *hal_path_keyboard_device(void) { return 0; }
const char *hal_path_keyboard_map(void) { return 0; }
const char *hal_path_images_dir(void) { return "/images"; }
const char *hal_path_audio_dir(void) { return "/audio"; }

int hal_process_exec_blocking(const char *exec_path, int keep_root)
{ (void)exec_path; (void)keep_root; return -1; }
hal_pid_t hal_process_spawn(const char *exec_path, int keep_root)
{ (void)exec_path; (void)keep_root; return -1; }
void hal_process_stop(hal_pid_t pid) { (void)pid; }
int hal_process_check_lock(const char *lock_path, int *holder_pid)
{ (void)lock_path; *holder_pid = 0; return 0; }
void hal_process_kill(int pid, int grace_ms) { (void)pid; (void)grace_ms; }
void hal_system_shutdown(void) {}
void hal_system_reboot(void) {}

hal_battery_info_t hal_battery_read(void)
{ hal_battery_info_t info; memset(&info, 0, sizeof(info)); return info; }
int hal_backlight_read(void) { return -1; }
int hal_backlight_max(void) { return 100; }
int hal_backlight_write(int val) { return val; }
int hal_volume_read(void) { return -1; }
int hal_volume_write(int val) { return val; }
hal_wifi_status_t hal_wifi_get_status(void)
{ hal_wifi_status_t st; memset(&st, 0, sizeof(st)); return st; }
int hal_wifi_scan(hal_wifi_ap_t *out, int max_aps) { (void)out; (void)max_aps; return 0; }
int hal_wifi_connect(const char *ssid, const char *password)
{ (void)ssid; (void)password; return -1; }
int hal_wifi_disconnect(void) { return 0; }
hal_bt_status_t hal_bt_get_status(void)
{ hal_bt_status_t st; memset(&st, 0, sizeof(st)); return st; }
int hal_bt_set_power(int on) { (void)on; return 0; }
int hal_bt_scan(hal_bt_device_t *out, int max_devices) { (void)out; (void)max_devices; return 0; }
void hal_time_str(char *buf, int buf_size)
{
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    if (t) snprintf(buf, buf_size, "%02d:%02d", t->tm_hour, t->tm_min);
    else if (buf_size > 0) buf[0] = '\0';
}
