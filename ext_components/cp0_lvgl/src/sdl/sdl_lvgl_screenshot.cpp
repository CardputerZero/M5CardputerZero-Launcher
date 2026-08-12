#include "hal_lvgl_bsp.h"
#include "lvgl/lvgl.h"
#include "lvgl/src/drivers/sdl/lv_sdl_window.h"
#include "../cp0_callback_contract.hpp"
#include "../cp0_display_screenshot_contract.hpp"
#include "../cp0_init_once.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <utility>
#include <SDL.h>

namespace {

static int mkdir_p(const char *dir)
{
    if (!dir || !dir[0])
        return -1;
    char tmp[512];
    std::snprintf(tmp, sizeof(tmp), "%s", dir);
    for (char *p = tmp + 1; *p; ++p) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    return mkdir(tmp, 0755) == 0 || errno == EEXIST ? 0 : -1;
}

class ScreenshotSystem {
public:
    using callback_t = std::function<void(int, std::string)>;
    using arg_t = std::list<std::string>;

    void api_call(arg_t arg, callback_t callback)
    {
        cp0::screenshot::Request request;
        if (!cp0::screenshot::parse_request(arg, request)) {
            report(callback, -1, cp0::screenshot::invalid_request_message());
            return;
        }
        std::string filename;
        int ret = save_to_bmp(request.directory.c_str(), filename);
        report(callback, ret, ret == 0 ? filename : "screenshot failed\n");
    }

private:
    static void report(const callback_t &callback, int code, const std::string &data)
    {
        cp0::callback::invoke(callback, code, data);
    }

    static int save_to_bmp(const char *dir, std::string &saved_path)
    {
        saved_path.clear();
        if (mkdir_p(dir) != 0)
            return -1;

        lv_display_t *disp = lv_display_get_default();
        if (!disp)
            return -2;
        lv_refr_now(disp);

        SDL_Renderer *renderer = static_cast<SDL_Renderer *>(lv_sdl_window_get_renderer(disp));
        if (!renderer)
            return -3;

        int w = 0;
        int h = 0;
        if (SDL_GetRendererOutputSize(renderer, &w, &h) != 0 ||
            !cp0::screenshot::valid_dimensions(w, h))
            return -4;

        SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
        if (!surface)
            return -5;

        int ret = 0;
        if (SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888, surface->pixels, surface->pitch) != 0) {
            ret = -6;
        } else {
            std::time_t now = std::time(nullptr);
            std::tm local{};
            if (!localtime_r(&now, &local)) {
                ret = -7;
            } else {
                cp0::screenshot::Timestamp timestamp{local.tm_year + 1900, local.tm_mon + 1,
                    local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec};
                std::string filename;
                if (!cp0::screenshot::make_output_path(dir, timestamp, true, filename)) {
                    ret = -7;
                } else if (SDL_SaveBMP(surface, filename.c_str()) != 0) {
                    ret = -8;
                } else {
                    std::printf("[SDL SCREENSHOT] Saved screenshot: %s\n", filename.c_str());
                    saved_path = filename;
                }
            }
        }

        SDL_FreeSurface(surface);
        return ret;
    }
};

} // namespace

extern "C" void init_screenshot(void)
{
    static cp0::InitOnce initialized;
    initialized.run([] {
        auto screenshot = std::make_shared<ScreenshotSystem>();
        return static_cast<bool>(cp0_signal_screenshot_api.append(
            [screenshot](std::list<std::string> arg,
                         std::function<void(int, std::string)> callback) {
                screenshot->api_call(std::move(arg), std::move(callback));
            }));
    });
}
