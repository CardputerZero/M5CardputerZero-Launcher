#include "hal_lvgl_bsp.h"
#include "../cp0_callback_contract.hpp"
#include "../cp0_display_screenshot_contract.hpp"
#include "../cp0_init_once.hpp"

#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

#include <fcntl.h>
#include <linux/fb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static void write_le16(FILE *f, uint16_t v) { fwrite(&v, 2, 1, f); }
static void write_le32(FILE *f, uint32_t v) { fwrite(&v, 4, 1, f); }

namespace {

class ScreenshotSystem {
public:
    typedef std::function<void(int, std::string)> callback_t;
    typedef std::list<std::string> arg_t;

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
        const char *fbdev = getenv("APPLAUNCH_LINUX_FBDEV_DEVICE");
        if (!fbdev) fbdev = "/dev/fb0";

        int fd = open(fbdev, O_RDONLY);
        if (fd < 0) return -1;

        struct fb_var_screeninfo vinfo;
        if (ioctl(fd, FBIOGET_VSCREENINFO, &vinfo) < 0) {
            close(fd);
            return -2;
        }

        int w = static_cast<int>(vinfo.xres);
        int h = static_cast<int>(vinfo.yres);
        int bpp = static_cast<int>(vinfo.bits_per_pixel);
        if (!cp0::screenshot::valid_dimensions(w, h) || (bpp != 16 && bpp != 32)) {
            close(fd);
            return -3;
        }
        int fb_line_len = w * (bpp / 8);

        struct fb_fix_screeninfo finfo;
        if (ioctl(fd, FBIOGET_FSCREENINFO, &finfo) == 0)
            fb_line_len = finfo.line_length;

        cp0::screenshot::FramebufferLayout layout;
        if (!cp0::screenshot::framebuffer_layout(w, h, bpp, fb_line_len, layout)) {
            close(fd);
            return -3;
        }
        void *fbmem = mmap(NULL, layout.mapped_size, PROT_READ, MAP_SHARED, fd, 0);
        if (fbmem == MAP_FAILED) {
            close(fd);
            return -3;
        }

        {
            struct stat st;
            if (stat(dir, &st) != 0) {
                char tmp[512];
                snprintf(tmp, sizeof(tmp), "%s", dir);
                for (char *p = tmp + 1; *p; ++p) {
                    if (*p == '/') { *p = 0; mkdir(tmp, 0755); *p = '/'; }
                }
                mkdir(tmp, 0755);
            }
        }

        time_t now = time(NULL);
        struct tm local = {};
        std::string filename;
        if (!localtime_r(&now, &local) ||
            !cp0::screenshot::make_output_path(dir,
                {local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
                    local.tm_hour, local.tm_min, local.tm_sec}, false, filename)) {
            munmap(fbmem, layout.mapped_size);
            close(fd);
            return -4;
        }

        FILE *fp = fopen(filename.c_str(), "wb");
        if (!fp) {
            munmap(fbmem, layout.mapped_size);
            close(fd);
            return -4;
        }

        int row_size = w * 3;
        int pad = static_cast<int>(layout.bmp_row_size) - row_size;

        fputc('B', fp); fputc('M', fp);
        write_le32(fp, layout.bmp_file_size);
        write_le16(fp, 0); write_le16(fp, 0);
        write_le32(fp, 54);
        write_le32(fp, 40);
        write_le32(fp, w);
        write_le32(fp, h);
        write_le16(fp, 1);
        write_le16(fp, 24);
        write_le32(fp, 0);
        write_le32(fp, layout.bmp_image_size);
        write_le32(fp, 2835); write_le32(fp, 2835);
        write_le32(fp, 0); write_le32(fp, 0);

        uint8_t padding[3] = {0};
        for (int y = h - 1; y >= 0; --y) {
            uint8_t *row = (uint8_t *)fbmem + y * fb_line_len;
            for (int x = 0; x < w; ++x) {
                uint8_t r, g, b;
                if (bpp == 16) {
                    uint16_t px = ((uint16_t *)row)[x];
                    r = ((px >> 11) & 0x1F) << 3;
                    g = ((px >> 5) & 0x3F) << 2;
                    b = (px & 0x1F) << 3;
                } else if (bpp == 32) {
                    uint32_t px = ((uint32_t *)row)[x];
                    r = (px >> vinfo.red.offset) & 0xFF;
                    g = (px >> vinfo.green.offset) & 0xFF;
                    b = (px >> vinfo.blue.offset) & 0xFF;
                } else {
                    r = g = b = 0;
                }
                uint8_t bgr[3] = {b, g, r};
                fwrite(bgr, 1, 3, fp);
            }
            if (pad > 0) fwrite(padding, 1, pad, fp);
        }

        int write_failed = ferror(fp);
        if (fclose(fp) != 0) write_failed = 1;
        munmap(fbmem, layout.mapped_size);
        close(fd);

        if (write_failed) return -5;

        printf("[SCREENSHOT] Saved: %s (%dx%d %dbpp)\n", filename.c_str(), w, h, bpp);
        saved_path = filename;
        return 0;
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
