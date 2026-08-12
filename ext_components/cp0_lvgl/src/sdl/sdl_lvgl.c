#include "hal_lvgl_bsp.h"
#include "lvgl/lvgl.h"

#include "input_keys.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sdl_lvgl.h"
#include "commount.h"
#include "../cp0_init_plan.h"
#include <pthread.h>

static uint64_t enabled_features(void)
{
    uint64_t features = 0;
#ifdef CONFIG_CP0_LVGL_INIT_FILESYSTEM
    features |= CP0_INIT_FEATURE_FILESYSTEM;
#endif
#ifdef CONFIG_CP0_LVGL_INIT_CONFIG
    features |= CP0_INIT_FEATURE_CONFIG;
#endif
#ifdef CONFIG_CP0_LVGL_INIT_PTY
    features |= CP0_INIT_FEATURE_PTY;
#endif
#ifdef CONFIG_CP0_LVGL_INIT_AUDIO
    features |= CP0_INIT_FEATURE_AUDIO;
#endif
#ifdef CONFIG_CP0_LVGL_INIT_PROCESS
    features |= CP0_INIT_FEATURE_PROCESS;
#endif
#ifdef CONFIG_CP0_LVGL_INIT_SUDO
    features |= CP0_INIT_FEATURE_SUDO;
#endif
#ifdef CONFIG_CP0_LVGL_INIT_OSINFO
    features |= CP0_INIT_FEATURE_OSINFO;
#endif
#ifdef CONFIG_CP0_LVGL_INIT_SCREENSHOT
    features |= CP0_INIT_FEATURE_SCREENSHOT;
#endif
#ifdef CONFIG_CP0_LVGL_INIT_LORA
    features |= CP0_INIT_FEATURE_LORA;
#endif
#ifdef CONFIG_CP0_LVGL_INIT_WIFI
    features |= CP0_INIT_FEATURE_WIFI;
#endif
#ifdef CONFIG_CP0_LVGL_INIT_SETTINGS
    features |= CP0_INIT_FEATURE_SETTINGS;
#endif
#ifdef CONFIG_CP0_LVGL_INIT_BQ27220
    features |= CP0_INIT_FEATURE_BQ27220;
#endif
#ifdef CONFIG_CP0_LVGL_INIT_SAVED_SETTINGS
    features |= CP0_INIT_FEATURE_SAVED_SETTINGS;
#endif
#ifdef CONFIG_CP0_LVGL_INIT_BATTERY
    features |= CP0_INIT_FEATURE_BATTERY;
#endif
#ifdef CONFIG_CP0_LVGL_INIT_CAMERA
    features |= CP0_INIT_FEATURE_CAMERA;
#endif
#ifdef CONFIG_CP0_LVGL_INIT_SOUNDCARD
    features |= CP0_INIT_FEATURE_SOUNDCARD;
#endif
    return features;
}

static int run_init_step(cp0_init_step_t step)
{
    switch (step) {
    case CP0_INIT_STEP_ENV: init_lvgl_env(); break;
    case CP0_INIT_STEP_EVENT: init_lvgl_event(); break;
    case CP0_INIT_STEP_FILESYSTEM:
#ifdef CONFIG_CP0_LVGL_INIT_FILESYSTEM
        init_filesystem();
#endif
        break;
    case CP0_INIT_STEP_CONFIG:
#ifdef CONFIG_CP0_LVGL_INIT_CONFIG
        init_config();
#endif
        break;
    case CP0_INIT_STEP_PTY:
#ifdef CONFIG_CP0_LVGL_INIT_PTY
        init_pty();
#endif
        break;
    case CP0_INIT_STEP_DISPLAY:
        init_freambuffer_disp();
        return lv_display_get_default() != NULL;
    case CP0_INIT_STEP_INPUT: init_input(); break;
    case CP0_INIT_STEP_AUDIO:
#ifdef CONFIG_CP0_LVGL_INIT_AUDIO
        init_audio();
#endif
        break;
    case CP0_INIT_STEP_PROCESS:
#ifdef CONFIG_CP0_LVGL_INIT_PROCESS
        init_process();
#endif
        break;
    case CP0_INIT_STEP_SUDO:
#ifdef CONFIG_CP0_LVGL_INIT_SUDO
        init_sudo_signals();
#endif
        break;
    case CP0_INIT_STEP_OSINFO:
#ifdef CONFIG_CP0_LVGL_INIT_OSINFO
        init_osinfo();
#endif
        break;
    case CP0_INIT_STEP_SCREENSHOT:
#ifdef CONFIG_CP0_LVGL_INIT_SCREENSHOT
        init_screenshot();
#endif
        break;
    case CP0_INIT_STEP_LORA:
#ifdef CONFIG_CP0_LVGL_INIT_LORA
        init_lora();
#endif
        break;
    case CP0_INIT_STEP_WIFI:
#ifdef CONFIG_CP0_LVGL_INIT_WIFI
        init_wifi();
#endif
        break;
    case CP0_INIT_STEP_SETTINGS:
#ifdef CONFIG_CP0_LVGL_INIT_SETTINGS
        init_settings();
#endif
        break;
    case CP0_INIT_STEP_BQ27220:
#ifdef CONFIG_CP0_LVGL_INIT_BQ27220
        init_bq27220();
#endif
        break;
    case CP0_INIT_STEP_SAVED_SETTINGS:
#ifdef CONFIG_CP0_LVGL_INIT_SAVED_SETTINGS
        init_lvgl_saved_settings();
#endif
        break;
    case CP0_INIT_STEP_BATTERY:
#ifdef CONFIG_CP0_LVGL_INIT_BATTERY
        init_battery();
#endif
        break;
    case CP0_INIT_STEP_CAMERA:
#ifdef CONFIG_CP0_LVGL_INIT_CAMERA
        init_camera();
#endif
        break;
    case CP0_INIT_STEP_SOUNDCARD:
#ifdef CONFIG_CP0_LVGL_INIT_SOUNDCARD
        init_soundcard();
#endif
        break;
    case CP0_INIT_STEP_RPC:
    case CP0_INIT_STEP_BLUETOOTH:
        break;
    }
    return 1;
}

void cp0_lvgl_init(void)
{
    static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    static size_t next_step = 0;
    cp0_init_step_t plan[CP0_INIT_STEP_SOUNDCARD + 1];
    const size_t step_count = cp0_build_init_plan(enabled_features(), plan,
        sizeof(plan) / sizeof(plan[0]));

    pthread_mutex_lock(&mutex);
    while (next_step < step_count) {
        if (!run_init_step(plan[next_step]))
            break;
        ++next_step;
    }
    pthread_mutex_unlock(&mutex);
}
