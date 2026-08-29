#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void        hal_paths_init(const char *exe_dir);
const char *hal_path_data_dir(void);
const char *hal_path_applications_dir(void);
const char *hal_path_lock_file(void);
const char *hal_path_font_dir(void);
const char *hal_path_font_regular(void);
const char *hal_path_font_mono(void);
const char *hal_path_keyboard_device(void);
const char *hal_path_keyboard_map(void);
const char *hal_path_images_dir(void);
const char *hal_path_audio_dir(void);
#ifdef __cplusplus
}
#endif
