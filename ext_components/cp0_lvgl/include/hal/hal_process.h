#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef int hal_pid_t;

int        hal_process_exec_blocking(const char *exec_path, int keep_root);
hal_pid_t  hal_process_spawn(const char *exec_path, int keep_root);
void       hal_process_stop(hal_pid_t pid);
int        hal_process_check_lock(const char *lock_path, int *holder_pid);
void       hal_process_kill(int pid, int grace_ms);
void       hal_system_shutdown(void);
void       hal_system_reboot(void);

#ifdef __cplusplus
}
#endif
