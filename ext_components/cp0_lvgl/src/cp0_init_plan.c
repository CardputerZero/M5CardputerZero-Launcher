#include "cp0_init_plan.h"

typedef struct optional_step {
    uint64_t feature;
    cp0_init_step_t step;
} optional_step_t;

static void append_step(cp0_init_step_t step, cp0_init_step_t *steps, size_t capacity, size_t *count)
{
    if (*count < capacity && steps != NULL)
        steps[*count] = step;
    ++(*count);
}

size_t cp0_build_init_plan(uint64_t features, cp0_init_step_t *steps, size_t capacity)
{
    static const optional_step_t before_display[] = {
        {CP0_INIT_FEATURE_FILESYSTEM, CP0_INIT_STEP_FILESYSTEM},
        {CP0_INIT_FEATURE_CONFIG, CP0_INIT_STEP_CONFIG},
        {CP0_INIT_FEATURE_PTY, CP0_INIT_STEP_PTY},
    };
    static const optional_step_t after_input[] = {
        {CP0_INIT_FEATURE_RPC, CP0_INIT_STEP_RPC},
        {CP0_INIT_FEATURE_AUDIO, CP0_INIT_STEP_AUDIO},
        {CP0_INIT_FEATURE_PROCESS, CP0_INIT_STEP_PROCESS},
        {CP0_INIT_FEATURE_SUDO, CP0_INIT_STEP_SUDO},
        {CP0_INIT_FEATURE_OSINFO, CP0_INIT_STEP_OSINFO},
        {CP0_INIT_FEATURE_SCREENSHOT, CP0_INIT_STEP_SCREENSHOT},
        {CP0_INIT_FEATURE_LORA, CP0_INIT_STEP_LORA},
        {CP0_INIT_FEATURE_WIFI, CP0_INIT_STEP_WIFI},
        {CP0_INIT_FEATURE_BLUETOOTH, CP0_INIT_STEP_BLUETOOTH},
        {CP0_INIT_FEATURE_SETTINGS, CP0_INIT_STEP_SETTINGS},
        {CP0_INIT_FEATURE_BQ27220, CP0_INIT_STEP_BQ27220},
        {CP0_INIT_FEATURE_SAVED_SETTINGS, CP0_INIT_STEP_SAVED_SETTINGS},
        {CP0_INIT_FEATURE_BATTERY, CP0_INIT_STEP_BATTERY},
        {CP0_INIT_FEATURE_CAMERA, CP0_INIT_STEP_CAMERA},
        {CP0_INIT_FEATURE_SOUNDCARD, CP0_INIT_STEP_SOUNDCARD},
    };
    size_t count = 0;
    size_t index;
    append_step(CP0_INIT_STEP_ENV, steps, capacity, &count);
    append_step(CP0_INIT_STEP_EVENT, steps, capacity, &count);
    for (index = 0; index < sizeof(before_display) / sizeof(before_display[0]); ++index)
        if ((features & before_display[index].feature) != 0)
            append_step(before_display[index].step, steps, capacity, &count);
    append_step(CP0_INIT_STEP_DISPLAY, steps, capacity, &count);
    append_step(CP0_INIT_STEP_INPUT, steps, capacity, &count);
    for (index = 0; index < sizeof(after_input) / sizeof(after_input[0]); ++index)
        if ((features & after_input[index].feature) != 0)
            append_step(after_input[index].step, steps, capacity, &count);
    return count;
}
