#include "cp0_lvgl_app.h"
#include "cp0_lora_backend.hpp"
#include "cp0_lora_device_discovery.hpp"
#include "cp0_lora_gpio.hpp"
#include "cp0_lora_hat_power_controller.hpp"
#include "cp0_lora_pi4io_controller.hpp"
#include "cp0_lora_runtime_policy.hpp"
#include "cp0_lora_spi_device.hpp"
#include "../cp0_app_internal_utils.h"
#include "hal_lvgl_bsp.h"

#include <atomic>
#include <cerrno>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <poll.h>
#include <string>
#include <unistd.h>

#ifndef SLOGI
#define SLOGI(fmt, ...) std::printf("[cp0_lora] " fmt "\n", ##__VA_ARGS__)
#endif

#if __has_include(<linux/gpio.h>)
#include <linux/gpio.h>
#define HAS_LINUX_GPIO_CDEV 1
#else
#define HAS_LINUX_GPIO_CDEV 0
#endif

#include "RadioLib.h"
#if __has_include(<lgpio.h>)
#include "hal/RPi/PiHal.h"
#else
class PiHal : public RadioLibHal {
public:
    PiHal(uint8_t spiChannel, uint32_t spiSpeed = 2000000, uint8_t spiDevice = 0, uint8_t gpioDevice = 0)
        : RadioLibHal(0, 1, 0, 1, 1, 2),
          _gpioDevice(gpioDevice),
          _spiDevice(spiDevice),
          _spiSpeed(spiSpeed),
          _spiChannel(spiChannel)
    {
    }

protected:
    uint8_t _gpioDevice;
    uint8_t _spiDevice;
    uint32_t _spiSpeed;
    uint8_t _spiChannel;
};
#endif

namespace cp0_lora_backend {

struct ExtPowerState {
    int ext5v = 0;
    int grove5v = 0;
    bool ext5v_changed = false;
    bool grove5v_changed = false;
    bool active = false;
};

static ExtPowerState g_ext_power;

static bool read_ext_power(const char *name, int *value)
{
    if (!name || !value) return false;
    bool valid = false;
    cp0_signal_settings_api({"GpioGet", name}, [&](int code, std::string response) {
        if (code == 0 && (response == "0" || response == "1")) {
            *value = response == "1" ? 1 : 0;
            valid = true;
        }
    });
    return valid;
}

static bool write_ext_power(const char *name, int value)
{
    if (!name) return false;
    int result = -1;
    cp0_signal_settings_api({"GpioSet", name, value ? "1" : "0"},
                             [&](int code, std::string) { result = code; });
    return result == 0;
}

static bool enable_ext_power()
{
    if (g_ext_power.active) return true;
    if (!read_ext_power("EXT5V", &g_ext_power.ext5v) ||
        !read_ext_power("GROVE5V", &g_ext_power.grove5v)) {
        g_ext_power = {};
        return false;
    }

    if (!g_ext_power.ext5v && !write_ext_power("EXT5V", 1)) {
        g_ext_power = {};
        return false;
    }
    g_ext_power.ext5v_changed = g_ext_power.ext5v == 0;
    if (!g_ext_power.grove5v && !write_ext_power("GROVE5V", 1)) {
        if (g_ext_power.ext5v_changed) (void)write_ext_power("EXT5V", g_ext_power.ext5v);
        g_ext_power = {};
        return false;
    }
    g_ext_power.grove5v_changed = g_ext_power.grove5v == 0;
    g_ext_power.active = true;
    return true;
}

static void restore_ext_power()
{
    if (!g_ext_power.active) return;
    if (g_ext_power.grove5v_changed) (void)write_ext_power("GROVE5V", g_ext_power.grove5v);
    if (g_ext_power.ext5v_changed) (void)write_ext_power("EXT5V", g_ext_power.ext5v);
    g_ext_power = {};
}

// ============================================================
//  Hardware configuration and state
// ============================================================
struct LoraRuntimeState {
    cp0_lora::SpiDevice spi;
    Module *radio_module = nullptr;
    SX1262 *radio        = nullptr;

    int power_gpio         = 5;
    int rst_gpio           = 26;
    int irq_gpio           = 23;
    int busy_gpio          = 22;
    int rst_fd             = -1;
    int busy_fd            = -1;
    int irq_fd             = -1;
    bool nss_manual        = false;
    bool irq_poll_fallback = true;

    volatile bool initialized = false;
    bool hw_ready             = false;
    bool tx_mode              = false;
    bool selected_tx_mode     = false;
    bool tx_in_progress       = false;
    bool pending_rx_after_tx  = false;
    volatile bool rx_done     = false;
    volatile bool tx_done     = false;
    bool rx_event             = false;
    bool tx_event             = false;
    bool has_sent_message     = false;

    uint32_t tx_counter      = 0;
    uint64_t tx_start_ms     = 0;
    uint64_t tx_timeout_ms   = cp0_lora_runtime_policy::TX_TIMEOUT_MS;
    uint64_t last_auto_tx_ms = 0;
    float last_rssi          = 0.0f;
    float last_snr           = 0.0f;
    char last_rx[128]        = {};
    char last_tx[128]        = "Hello from M5 LoRa-1262";
    char last_diag[256]      = "idle";
    char probe_summary[256]  = "probe not started";
    char probe_display[128]  = "SPI: probing...";

    void reset_runtime_flags()
    {
        initialized         = false;
        hw_ready            = false;
        tx_mode             = false;
        selected_tx_mode    = false;
        tx_in_progress      = false;
        pending_rx_after_tx = false;
        rx_done             = false;
        tx_done             = false;
        rx_event            = false;
        tx_event            = false;
        tx_start_ms         = 0;
        tx_timeout_ms       = cp0_lora_runtime_policy::TX_TIMEOUT_MS;
        last_auto_tx_ms     = 0;
    }

    void reset_radio()
    {
        delete radio;
        radio = nullptr;
        delete radio_module;
        radio_module = nullptr;
    }

    void close_gpio_lines()
    {
        close_fd(rst_fd);
        close_fd(busy_fd);
        close_fd(irq_fd);
    }

private:
    static void close_fd(int &fd)
    {
        if (fd >= 0) {
            ::close(fd);
            fd = -1;
        }
    }
};

static LoraRuntimeState g_lora;
static std::atomic<bool> g_stop_requested{false};

static bool lora_stop_requested()
{
    return g_stop_requested.load(std::memory_order_acquire);
}

// Forward declarations
static uint64_t get_monotonic_ms(void);
static bool lora_spi_transfer(const uint8_t *tx, uint8_t *rx, size_t len);
static void resolve_lora_spi_device(void);
static bool probe_lora_spi_device(void);
static void lora_apply_mode(bool tx_mode);
static void lora_start_receive_mode(void);
static void lora_send_demo_packet(void);
static void lora_service_irq_once(void);
static void lora_check_tx_fallback(void);
static void lora_set_diag_step(const char *step, int code, const char *detail);
static const char *lora_radiolib_status_text(int16_t state);
static bool lora_send_text_packet(const char *payload);
static void lora_init_hardware(void);

static void lora_release_hardware()
{
    g_lora.reset_radio();
    g_lora.spi.close();
    g_lora.close_gpio_lines();
    cp0_lora_hat_power_controller::shutdown();
    restore_ext_power();
    g_lora.reset_runtime_flags();
}

static bool lora_spi_transfer(const uint8_t *tx, uint8_t *rx, size_t len)
{
    return g_lora.spi.transfer(tx, rx, len);
}

static bool lora_open_runtime_spi(void)
{
    if (g_lora.spi.open()) return true;
    snprintf(g_lora.last_diag, sizeof(g_lora.last_diag), "runtime SPI open/config failed on %s", g_lora.spi.path());
    return false;
}

static bool sx1262_wait_while_busy(unsigned int timeout_ms)
{
    const unsigned int sleep_us = 1000;
    unsigned int waited_ms      = 0;
    while (waited_ms < timeout_ms) {
        if (lora_stop_requested()) return false;
        int busy = gpio_get_value_any(g_lora.busy_gpio, g_lora.busy_fd);
        if (busy < 0) return false;
        if (busy == 0) return true;
        usleep(sleep_us);
        waited_ms += 1;
    }
    return false;
}

static bool sx1262_reset(void)
{
    if (lora_stop_requested()) return false;
    if (gpio_set_value_any(g_lora.rst_gpio, g_lora.rst_fd, 0) < 0) return false;
    usleep(20000);
    if (gpio_set_value_any(g_lora.rst_gpio, g_lora.rst_fd, 1) < 0) return false;
    usleep(10000);
    return sx1262_wait_while_busy(200);
}

static bool sx1262_get_status_raw(uint8_t *status)
{
    uint8_t tx[2] = {0xC0, 0x00};
    uint8_t rx[2] = {0};
    if (!status) return false;
    if (!lora_spi_transfer(tx, rx, sizeof(tx))) return false;
    *status = rx[1];
    return true;
}

static bool probe_lora_spi_device(void)
{
    const char *spi_env          = getenv("LORA_SPI_DEV");
    char candidates[16][64]      = {{0}};
    const size_t candidate_count = cp0_lora_device_discovery::collect_spi_candidates(candidates, 16, spi_env);
    char summary[256]            = {0};

    if (access("/dev", F_OK) != 0) {
        snprintf(g_lora.last_diag, sizeof(g_lora.last_diag),
                 "Linux /dev not available; LoRa SPI HAL requires Raspberry Pi Linux runtime");
        snprintf(g_lora.probe_summary, sizeof(g_lora.probe_summary), "no /dev directory visible");
        snprintf(g_lora.probe_display, sizeof(g_lora.probe_display), "SPI: /dev unavailable");
        return false;
    }
    if (candidate_count == 0) {
        snprintf(g_lora.last_diag, sizeof(g_lora.last_diag), "no /dev/spidev* found; enable SPI on Raspberry Pi OS");
        snprintf(g_lora.probe_summary, sizeof(g_lora.probe_summary), "probe aborted: no spidev nodes");
        snprintf(g_lora.probe_display, sizeof(g_lora.probe_display), "SPI: no spidev found");
        return false;
    }
    SLOGI("LoRa SPI probe policy: prefer SPI0 only, CE1 then CE0");
    summary[0] = '\0';
    for (size_t i = 0; i < candidate_count; ++i) {
        const char *dev = candidates[i];
        if (spi_env && spi_env[0] && strcmp(spi_env, dev) == 0) continue;
        if (summary[0]) strncat(summary, ", ", sizeof(summary) - strlen(summary) - 1);
        strncat(summary, dev, sizeof(summary) - strlen(summary) - 1);
    }
    if (spi_env && spi_env[0]) {
        snprintf(g_lora.probe_summary, sizeof(g_lora.probe_summary), "probe order: %.96s%s%.128s", spi_env,
                 summary[0] ? ", " : "", summary);
        snprintf(g_lora.probe_display, sizeof(g_lora.probe_display), "Try: %.96s -> 0.1 -> 0.0", spi_env);
    } else {
        snprintf(g_lora.probe_summary, sizeof(g_lora.probe_summary), "probe order: %.224s", summary);
        snprintf(g_lora.probe_display, sizeof(g_lora.probe_display), "Try: /dev/spidev0.1 -> /dev/spidev0.0");
    }

    auto try_probe = [](const char *dev) -> bool {
        if (dev == NULL || dev[0] == '\0' || access(dev, F_OK) != 0) return false;
        g_lora.spi.set_path(dev);
        g_lora.nss_manual    = false;
        const char *spi_path = g_lora.spi.path();
        const char *cs_name =
            strstr(spi_path, "spidev0.1") ? "SPI0-CE1" : (strstr(spi_path, "spidev0.0") ? "SPI0-CE0" : "non-SPI0");
        SLOGI("LoRa probe: trying %s [%s] (cs=hw-auto)", spi_path, cs_name);
        g_lora.initialized = false;
        g_lora.spi.close();
        if (gpio_init_output_any("LORA_RST_CHIP", "LORA_RST_OFFSET", g_lora.rst_gpio, 1, &g_lora.rst_fd, "RST") < 0) {
            snprintf(g_lora.last_diag, sizeof(g_lora.last_diag), "RST gpio init failed on %s", spi_path);
            return false;
        }
        if (!sx1262_reset()) {
            snprintf(g_lora.last_diag, sizeof(g_lora.last_diag), "RST/BUSY handshake failed on %s", spi_path);
            return false;
        }
        uint8_t status = 0;
        if (!g_lora.spi.open()) {
            snprintf(g_lora.last_diag, sizeof(g_lora.last_diag), "SPI open/config failed on %s", spi_path);
            return false;
        }
        bool ok = sx1262_get_status_raw(&status);
        g_lora.spi.close();
        if (!ok) {
            snprintf(g_lora.last_diag, sizeof(g_lora.last_diag), "status read failed on %s", spi_path);
            return false;
        }
        SLOGI("LoRa probe: %s [%s] (cs=hw-auto) status=0x%02X", spi_path, cs_name, status);
        snprintf(g_lora.last_diag, sizeof(g_lora.last_diag), "probe ok on %s[%s] cs=hw-auto status=0x%02X", spi_path,
                 cs_name, status);
        snprintf(g_lora.probe_display, sizeof(g_lora.probe_display), "FOUND: %s (%s)", spi_path, cs_name);
        return true;
    };

    if (spi_env && spi_env[0] && try_probe(spi_env)) return true;
    for (size_t i = 0; i < candidate_count; ++i) {
        if (try_probe(candidates[i])) return true;
    }
    snprintf(g_lora.last_diag, sizeof(g_lora.last_diag), "all SPI buses probed, no SX1262 response (%.192s)",
             g_lora.probe_summary);
    snprintf(g_lora.probe_display, sizeof(g_lora.probe_display), "NOT FOUND: tried 0.1 and 0.0");
    return false;
}

static void resolve_lora_spi_device(void)
{
    const char *spi_env          = getenv("LORA_SPI_DEV");
    char candidates[16][64]      = {{0}};
    const size_t candidate_count = cp0_lora_device_discovery::collect_spi_candidates(candidates, 16, spi_env);
    if (spi_env != NULL && spi_env[0] != '\0' && access(spi_env, F_OK) == 0) {
        g_lora.spi.set_path(spi_env);
        return;
    }
    for (size_t i = 0; i < candidate_count; ++i) {
        if (access(candidates[i], F_OK) == 0) {
            g_lora.spi.set_path(candidates[i]);
            return;
        }
    }
    g_lora.spi.set_path(spi_env && spi_env[0] ? spi_env : "/dev/spidev0.1");
}

// ============================================================
//  RadioLib HAL / Module / TX/RX logic
// ============================================================

class LinuxRadioLibHal : public PiHal {
public:
    LinuxRadioLibHal() : PiHal(0, 2000000, 0, 0)
    {
    }

    void pinMode(uint32_t pin, uint32_t mode) override
    {
        if (pin == RADIOLIB_NC) return;
        if (mode == GpioModeOutput) {
            if (pin == (uint32_t)g_lora.rst_gpio) {
                (void)gpio_init_output_any("LORA_RST_CHIP", "LORA_RST_OFFSET", (int)pin, 1, &g_lora.rst_fd, "RST");
            }
        } else {
            if (pin == (uint32_t)g_lora.busy_gpio) {
                (void)gpio_init_input_any("LORA_BUSY_CHIP", "LORA_BUSY_OFFSET", (int)pin, &g_lora.busy_fd, "BUSY");
            }
        }
    }

    void digitalWrite(uint32_t pin, uint32_t value) override
    {
        if (pin == RADIOLIB_NC) return;
        int line_fd = -1;
        if (pin == (uint32_t)g_lora.rst_gpio) line_fd = g_lora.rst_fd;
        (void)gpio_set_value_any((int)pin, line_fd, value ? 1 : 0);
    }

    uint32_t digitalRead(uint32_t pin) override
    {
        if (pin == RADIOLIB_NC) return 0;
        int line_fd = -1;
        if (pin == (uint32_t)g_lora.busy_gpio) line_fd = g_lora.busy_fd;
        int value = gpio_get_value_any((int)pin, line_fd);
        return value > 0 ? 1U : 0U;
    }

    void attachInterrupt(uint32_t, void (*)(void), uint32_t) override
    {
    }
    void detachInterrupt(uint32_t) override
    {
    }
    void delay(RadioLibTime_t ms) override
    {
        while (ms > 0 && !lora_stop_requested()) {
            const RadioLibTime_t slice = ms > 10 ? 10 : ms;
            usleep((useconds_t)(slice * 1000));
            ms -= slice;
        }
    }
    void delayMicroseconds(RadioLibTime_t us) override
    {
        while (us > 0 && !lora_stop_requested()) {
            const RadioLibTime_t slice = us > 10000 ? 10000 : us;
            usleep((useconds_t)slice);
            us -= slice;
        }
    }
    RadioLibTime_t millis() override
    {
        return (RadioLibTime_t)get_monotonic_ms();
    }
    RadioLibTime_t micros() override
    {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (RadioLibTime_t)ts.tv_sec * 1000000ULL + (RadioLibTime_t)ts.tv_nsec / 1000ULL;
    }
    long pulseIn(uint32_t pin, uint32_t state, RadioLibTime_t timeout) override
    {
        RadioLibTime_t start = micros();
        while (micros() - start < timeout) {
            if (digitalRead(pin) == state) {
                RadioLibTime_t pulse_start = micros();
                while (micros() - start < timeout && digitalRead(pin) == state) {
                }
                return (long)(micros() - pulse_start);
            }
        }
        return 0;
    }
    void spiBegin() override
    {
    }
    void spiBeginTransaction() override
    {
    }
    void spiTransfer(uint8_t *out, size_t len, uint8_t *in) override
    {
        if (lora_stop_requested()) {
            if (in) memset(in, 0, len);
            return;
        }
        uint8_t dummy[512] = {0};
        uint8_t *tx        = out ? out : dummy;
        uint8_t *rx        = in ? in : dummy;
        if (len > sizeof(dummy)) len = sizeof(dummy);
        (void)lora_spi_transfer(tx, rx, len);
    }
    void spiEndTransaction() override
    {
    }
    void spiEnd() override
    {
    }
};

static LinuxRadioLibHal g_lora_radio_hal;

static uint64_t get_monotonic_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

static void lora_set_diag_step(const char *step, int code, const char *detail)
{
    char detail_copy[sizeof(g_lora.last_diag)] = {};
    if (detail && detail[0]) snprintf(detail_copy, sizeof(detail_copy), "%s", detail);
    snprintf(g_lora.last_diag, sizeof(g_lora.last_diag), "%s%s%s | rc=%d", step ? step : "diag",
             detail_copy[0] ? " | " : "", detail_copy, code);
    SLOGI("LoRa diag: %s", g_lora.last_diag);
}

static const char *lora_radiolib_status_text(int16_t state)
{
    switch (state) {
        case RADIOLIB_ERR_NONE:
            return "ok";
        case RADIOLIB_ERR_CHIP_NOT_FOUND:
            return "chip_not_found";
        case RADIOLIB_ERR_TX_TIMEOUT:
            return "tx_timeout";
        case RADIOLIB_ERR_RX_TIMEOUT:
            return "rx_timeout";
        case RADIOLIB_ERR_CRC_MISMATCH:
            return "crc_mismatch";
        case RADIOLIB_ERR_SPI_WRITE_FAILED:
            return "spi_write_failed";
        case RADIOLIB_ERR_SPI_CMD_TIMEOUT:
            return "spi_cmd_timeout";
        case RADIOLIB_ERR_SPI_CMD_INVALID:
            return "spi_cmd_invalid";
        case RADIOLIB_ERR_SPI_CMD_FAILED:
            return "spi_cmd_failed";
        default:
            return "radiolib_err";
    }
}

static uint64_t lora_tx_timeout_for_payload(size_t payload_length)
{
    const uint64_t airtime_us =
        g_lora.radio == nullptr ? 0 : static_cast<uint64_t>(g_lora.radio->getTimeOnAir(payload_length));
    const uint64_t timeout_ms = cp0_lora_runtime_policy::tx_timeout_for_airtime_us(airtime_us);
    SLOGI("LoRa TX: payload=%zu airtime=%lluus watchdog=%llums", payload_length,
          static_cast<unsigned long long>(airtime_us), static_cast<unsigned long long>(timeout_ms));
    return timeout_ms;
}

static void lora_capture_device_errors(const char *stage, uint16_t irq_status)
{
    if (!g_lora.initialized || g_lora.radio == NULL) return;
    SLOGI("LoRa error: %s irq=0x%04X", stage ? stage : "radio_err", irq_status);
    snprintf(g_lora.last_diag, sizeof(g_lora.last_diag), "%s irq=0x%04X", stage ? stage : "radio_err", irq_status);
}

static bool lora_send_text_packet(const char *payload)
{
    if (!g_lora.initialized || g_lora.radio == NULL) {
        SLOGI("LoRa TX: not initialized");
        return false;
    }
    if (payload == NULL || payload[0] == '\0') return false;
    if (g_lora.tx_in_progress) return false;
    snprintf(g_lora.last_tx, sizeof(g_lora.last_tx), "%s", payload);
    g_lora.has_sent_message    = true;
    g_lora.tx_done             = false;
    g_lora.rx_done             = false;
    g_lora.pending_rx_after_tx = true;
    g_lora.tx_mode             = false;
    g_lora.selected_tx_mode    = false;
    (void)g_lora.radio->standby();
    const size_t payload_length = strlen(g_lora.last_tx);
    g_lora.tx_timeout_ms        = lora_tx_timeout_for_payload(payload_length);
    int16_t state               = g_lora.radio->startTransmit((uint8_t *)g_lora.last_tx, payload_length);
    if (state != RADIOLIB_ERR_NONE) {
        g_lora.tx_in_progress      = false;
        g_lora.tx_start_ms         = 0;
        g_lora.pending_rx_after_tx = false;
        g_lora.tx_timeout_ms       = cp0_lora_runtime_policy::TX_TIMEOUT_MS;
        SLOGI("LoRa TX: startTransmit failed rc=%d(%s)", (int)state, lora_radiolib_status_text(state));
        lora_start_receive_mode();
        return false;
    }
    g_lora.tx_in_progress = true;
    g_lora.tx_start_ms = g_lora.last_auto_tx_ms = get_monotonic_ms();
    SLOGI("LoRa TX: sending '%s'", g_lora.last_tx);
    return true;
}

static void lora_send_demo_packet(void)
{
    if (!g_lora.initialized || g_lora.radio == NULL) return;
    if (!g_lora.tx_mode) return;
    snprintf(g_lora.last_tx, sizeof(g_lora.last_tx), "Hello from M5 LoRa-1262 #%lu", (unsigned long)g_lora.tx_counter);
    g_lora.has_sent_message     = true;
    g_lora.pending_rx_after_tx  = false;
    g_lora.tx_done              = false;
    g_lora.rx_done              = false;
    const size_t payload_length = strlen(g_lora.last_tx);
    g_lora.tx_timeout_ms        = lora_tx_timeout_for_payload(payload_length);
    int16_t state               = g_lora.radio->startTransmit((uint8_t *)g_lora.last_tx, payload_length);
    if (state != RADIOLIB_ERR_NONE) {
        g_lora.tx_in_progress = false;
        g_lora.tx_start_ms    = 0;
        g_lora.tx_timeout_ms  = cp0_lora_runtime_policy::TX_TIMEOUT_MS;
        SLOGI("LoRa TX: demo startTransmit failed rc=%d(%s)", (int)state, lora_radiolib_status_text(state));
        return;
    }
    g_lora.tx_in_progress = true;
    g_lora.tx_start_ms = g_lora.last_auto_tx_ms = get_monotonic_ms();
    SLOGI("LoRa TX: demo sending '%s'", g_lora.last_tx);
    ++g_lora.tx_counter;
}

static void lora_start_receive_mode(void)
{
    if (!g_lora.initialized || g_lora.radio == NULL) {
        SLOGI("LoRa RX: startReceive skipped, not initialized");
        return;
    }
    if (g_lora.tx_in_progress) {
        SLOGI("LoRa RX: startReceive skipped, TX in progress");
        g_lora.pending_rx_after_tx = true;
        return;
    }
    g_lora.tx_mode             = false;
    g_lora.selected_tx_mode    = false;
    g_lora.pending_rx_after_tx = false;
    SLOGI("LoRa RX: startReceive()");
    int16_t state = g_lora.radio->startReceive();
    SLOGI("LoRa RX: startReceive rc=%d(%s)", (int)state, lora_radiolib_status_text(state));
    if (state != RADIOLIB_ERR_NONE) {
        snprintf(g_lora.last_diag, sizeof(g_lora.last_diag), "startReceive rc=%d(%s)", (int)state,
                 lora_radiolib_status_text(state));
    }
}

static void lora_apply_mode(bool tx_mode)
{
    g_lora.selected_tx_mode = tx_mode;
    if (!g_lora.initialized || g_lora.radio == NULL) {
        SLOGI("LoRa mode: not initialized");
        return;
    }
    if (tx_mode) {
        g_lora.pending_rx_after_tx = false;
        g_lora.tx_mode             = true;
        g_lora.last_auto_tx_ms     = get_monotonic_ms();
        if (g_lora.tx_in_progress) {
            SLOGI("LoRa mode: TX already in progress");
            return;
        }
        int16_t state = g_lora.radio->standby();
        if (state == RADIOLIB_ERR_NONE) {
            SLOGI("LoRa mode: TX ready");
        } else {
            SLOGI("LoRa mode: set TX failed rc=%d(%s)", (int)state, lora_radiolib_status_text(state));
        }
    } else {
        if (g_lora.tx_in_progress) {
            g_lora.pending_rx_after_tx = true;
            SLOGI("LoRa mode: TX in progress, will RX after done");
            return;
        }
        g_lora.pending_rx_after_tx = false;
        g_lora.tx_mode             = false;
        g_lora.last_auto_tx_ms     = get_monotonic_ms();
        lora_start_receive_mode();
    }
}

static void lora_service_irq_once(void)
{
    if (!g_lora.initialized || g_lora.radio == NULL) return;

    bool irq_event = false;
    if (!g_lora.irq_poll_fallback && g_lora.irq_fd >= 0) {
        struct pollfd pfd;
        memset(&pfd, 0, sizeof(pfd));
        pfd.fd     = g_lora.irq_fd;
        pfd.events = POLLIN | POLLPRI;
        if (poll(&pfd, 1, 0) > 0 && (pfd.revents & (POLLIN | POLLPRI))) {
            irq_event = true;
#if HAS_LINUX_GPIO_CDEV
            struct gpioevent_data event_data;
            while (read(g_lora.irq_fd, &event_data, sizeof(event_data)) == (ssize_t)sizeof(event_data)) {
            }
#else
            char value_buf[8];
            lseek(g_lora.irq_fd, 0, SEEK_SET);
            while (read(g_lora.irq_fd, value_buf, sizeof(value_buf)) > 0) {
                lseek(g_lora.irq_fd, 0, SEEK_SET);
                break;
            }
#endif
        }
    }

    uint32_t irq_flags = g_lora.radio->getIrqFlags();
    if (irq_flags != RADIOLIB_SX126X_IRQ_NONE || irq_event) {
        SLOGI("LoRa IRQ: event=%d flags=0x%08lX tx_in_progress=%d tx_mode=%d", irq_event ? 1 : 0,
              (unsigned long)irq_flags, g_lora.tx_in_progress ? 1 : 0, g_lora.tx_mode ? 1 : 0);
    }
    if (!irq_event && irq_flags == RADIOLIB_SX126X_IRQ_NONE) return;

    if (g_lora.tx_in_progress) {
        if (irq_flags & RADIOLIB_SX126X_IRQ_TX_DONE) {
            int16_t state = g_lora.radio->finishTransmit();
            if (state == RADIOLIB_ERR_NONE) {
                g_lora.tx_done = true;
            } else {
                g_lora.tx_in_progress = false;
                g_lora.tx_start_ms    = 0;
                g_lora.tx_timeout_ms  = cp0_lora_runtime_policy::TX_TIMEOUT_MS;
                snprintf(g_lora.last_diag, sizeof(g_lora.last_diag), "finishTransmit rc=%d(%s)", (int)state,
                         lora_radiolib_status_text(state));
                SLOGI("LoRa TX: finishTransmit failed rc=%d(%s)", (int)state, lora_radiolib_status_text(state));
                if (g_lora.pending_rx_after_tx || !g_lora.tx_mode) lora_start_receive_mode();
            }
        } else if (irq_flags & RADIOLIB_SX126X_IRQ_TIMEOUT) {
            g_lora.tx_in_progress = false;
            g_lora.tx_start_ms    = 0;
            g_lora.tx_timeout_ms  = cp0_lora_runtime_policy::TX_TIMEOUT_MS;
            lora_capture_device_errors("TX irq timeout", 0);
            if (g_lora.pending_rx_after_tx || !g_lora.tx_mode) lora_start_receive_mode();
        }
        return;
    }

    if (irq_flags & RADIOLIB_SX126X_IRQ_RX_DONE) {
        uint8_t rx_buf[sizeof(g_lora.last_rx)] = {0};
        int16_t state                          = g_lora.radio->readData(rx_buf, sizeof(g_lora.last_rx) - 1);
        SLOGI("LoRa RX: readData rc=%d(%s)", (int)state, lora_radiolib_status_text(state));
        if (state == RADIOLIB_ERR_NONE) {
            memcpy(g_lora.last_rx, rx_buf, sizeof(g_lora.last_rx));
            g_lora.last_rx[sizeof(g_lora.last_rx) - 1] = '\0';
            g_lora.last_rssi                           = g_lora.radio->getRSSI();
            g_lora.last_snr                            = g_lora.radio->getSNR();
            g_lora.rx_done                             = true;
            SLOGI("LoRa RX OK: '%s' RSSI=%.1f SNR=%.1f", g_lora.last_rx, g_lora.last_rssi, g_lora.last_snr);
        } else if (state != RADIOLIB_ERR_CRC_MISMATCH) {
            snprintf(g_lora.last_diag, sizeof(g_lora.last_diag), "readData rc=%d(%s)", (int)state,
                     lora_radiolib_status_text(state));
        }
        if (!g_lora.tx_mode) lora_start_receive_mode();
    } else if (irq_flags & (RADIOLIB_SX126X_IRQ_CRC_ERR | RADIOLIB_SX126X_IRQ_HEADER_ERR)) {
        snprintf(g_lora.last_diag, sizeof(g_lora.last_diag), "RX crc/header error irq=0x%04lX",
                 (unsigned long)irq_flags);
        SLOGI("LoRa RX error: %s", g_lora.last_diag);
        if (!g_lora.tx_mode) lora_start_receive_mode();
    } else if (irq_flags & RADIOLIB_SX126X_IRQ_TIMEOUT) {
        snprintf(g_lora.last_diag, sizeof(g_lora.last_diag), "RX timeout irq=0x%04lX", (unsigned long)irq_flags);
        SLOGI("LoRa RX timeout: %s", g_lora.last_diag);
    }
}

static void lora_check_tx_fallback(void)
{
    uint64_t now_ms = get_monotonic_ms();
    if (cp0_lora_runtime_policy::should_timeout_transmit(g_lora.initialized, g_lora.tx_in_progress,
                                                         g_lora.radio != NULL, g_lora.tx_start_ms, now_ms,
                                                         g_lora.tx_timeout_ms)) {
        g_lora.tx_in_progress  = false;
        g_lora.tx_start_ms     = 0;
        g_lora.tx_timeout_ms   = cp0_lora_runtime_policy::TX_TIMEOUT_MS;
        g_lora.last_auto_tx_ms = now_ms;
        lora_capture_device_errors("TX timeout", 0);
        (void)g_lora.radio->standby();
        if (g_lora.pending_rx_after_tx || !g_lora.tx_mode) lora_start_receive_mode();
    }
}

static void lora_poll_hardware(void)
{
    if (!g_lora.initialized) return;
    lora_service_irq_once();
    lora_check_tx_fallback();

    if (g_lora.tx_done) {
        g_lora.tx_done        = false;
        g_lora.tx_event       = true;
        g_lora.tx_in_progress = false;
        g_lora.tx_start_ms    = 0;
        g_lora.tx_timeout_ms  = cp0_lora_runtime_policy::TX_TIMEOUT_MS;
        if (g_lora.pending_rx_after_tx || !g_lora.tx_mode) {
            lora_start_receive_mode();
        }
    }

    if (g_lora.rx_done) {
        g_lora.rx_done  = false;
        g_lora.rx_event = true;
    }

    uint64_t now_ms = get_monotonic_ms();
    if (cp0_lora_runtime_policy::should_send_demo(g_lora.initialized, g_lora.tx_mode, g_lora.tx_in_progress,
                                                  g_lora.last_auto_tx_ms, now_ms)) {
        lora_send_demo_packet();
    }
}

// ============================================================
//  Hardware initialization
// ============================================================

static void lora_init_hardware(void)
{
    if (lora_stop_requested()) return;
    // A failed probe can leave SPI/GPIO handles allocated. Release all of
    // them before retrying so entering the page again is deterministic.
    lora_release_hardware();

    lora_set_diag_step("ext_power_enable", 0, "enable EXT5V and GROVE5V outputs");
    if (!enable_ext_power()) {
        lora_set_diag_step("ext_power_enable", 1, "failed to enable EXT5V/GROVE5V outputs");
        lora_release_hardware();
        return;
    }

    lora_set_diag_step("i2c_scan", 0, "scan 0x43 before LoRa init");
    if (cp0_lora_pi4io_controller::scan_and_initialize()) {
        lora_set_diag_step("i2c_scan", 0, cp0_lora_pi4io_controller::status());
    } else {
        lora_set_diag_step("i2c_scan", 1, cp0_lora_pi4io_controller::status());
    }

    lora_set_diag_step("power_enable", 0, "start");
    if (!cp0_lora_hat_power_controller::enable(g_lora.power_gpio)) {
        SLOGI("Status: GPIO5 low set failed");
        lora_set_diag_step("power_enable", 1, "GPIO5 low set failed");
    }
    usleep(100000);
    if (lora_stop_requested()) {
        lora_release_hardware();
        return;
    }

    lora_set_diag_step("reset_gpio_init", 0, "prepare rst pin");
    if (gpio_init_output_any("LORA_RST_CHIP", "LORA_RST_OFFSET", g_lora.rst_gpio, 1, &g_lora.rst_fd, "RST") < 0) {
        g_lora.initialized = false;
        g_lora.hw_ready    = false;
        lora_set_diag_step("reset_gpio_init", 1, "rst gpio init failed");
        lora_release_hardware();
        return;
    }

    if (gpio_init_input_any("LORA_BUSY_CHIP", "LORA_BUSY_OFFSET", g_lora.busy_gpio, &g_lora.busy_fd, "BUSY") < 0) {
        g_lora.initialized = false;
        g_lora.hw_ready    = false;
        lora_set_diag_step("busy_gpio_init", 1, "busy gpio init failed");
        lora_release_hardware();
        return;
    }

    lora_set_diag_step("hard_reset", 0, "toggle rst before probe");
    if (!sx1262_reset()) {
        g_lora.initialized = false;
        g_lora.hw_ready    = false;
        lora_set_diag_step("hard_reset", 1, "rst/busy handshake failed");
        lora_release_hardware();
        return;
    }

    lora_set_diag_step("resolve_spi", 0, "detect device");
    resolve_lora_spi_device();

    if (!probe_lora_spi_device()) {
        g_lora.initialized = false;
        g_lora.hw_ready    = false;
        lora_set_diag_step("probe_spi", 1, g_lora.last_diag);
        lora_release_hardware();
        return;
    }

    lora_set_diag_step("pre_begin_prepare", 0, "reset again before RadioLib begin");
    if (!sx1262_reset()) {
        g_lora.initialized = false;
        g_lora.hw_ready    = false;
        lora_set_diag_step("pre_begin_prepare", 1, "rst/busy handshake failed before RadioLib begin");
        lora_release_hardware();
        return;
    }

    lora_set_diag_step("prepare_irq", 0, "init irq pin");
    if (gpio_init_input_irq_any("LORA_IRQ_CHIP", "LORA_IRQ_OFFSET", g_lora.irq_gpio, &g_lora.irq_fd, "IRQ") < 0) {
        g_lora.irq_poll_fallback = true;
        lora_set_diag_step("prepare_irq", 1, "irq gpio init failed, fallback=poll");
    } else {
        g_lora.irq_poll_fallback = false;
        lora_set_diag_step("prepare_irq", 0, "irq gpio ok");
    }

    lora_set_diag_step("runtime_spi", 0, "open SPI for RadioLib runtime");
    if (!lora_open_runtime_spi()) {
        g_lora.initialized = false;
        g_lora.hw_ready    = false;
        lora_set_diag_step("runtime_spi", 1, g_lora.last_diag);
        lora_release_hardware();
        return;
    }

    lora_set_diag_step("radiolib_setup", 0, "create module");
    g_lora.nss_manual   = false;
    g_lora.radio_module = new Module(&g_lora_radio_hal, RADIOLIB_NC, (uint32_t)g_lora.irq_gpio,
                                     (uint32_t)g_lora.rst_gpio, (uint32_t)g_lora.busy_gpio);
    g_lora.radio        = new SX1262(g_lora.radio_module);

    if (g_lora.radio_module == NULL || g_lora.radio == NULL) {
        g_lora.initialized = false;
        g_lora.hw_ready    = false;
        lora_set_diag_step("radiolib_setup", 1, "allocation failed");
        lora_release_hardware();
        return;
    }

    lora_set_diag_step("radiolib_begin", 0, "configure sx1262 via RadioLib");
    int16_t state = g_lora.radio->begin(868.0f,  // frequency MHz
                                        125.0f,  // bandwidth kHz
                                        12,      // spreading factor
                                        5,       // coding rate 4/5
                                        0x34,    // sync word
                                        22,      // output power dBm
                                        20,      // preamble length
                                        3.0f,    // TCXO voltage
                                        false);

    if (lora_stop_requested()) {
        lora_release_hardware();
        return;
    }

    if (state != RADIOLIB_ERR_NONE) {
        g_lora.initialized = false;
        g_lora.hw_ready    = false;
        snprintf(g_lora.last_diag, sizeof(g_lora.last_diag), "RadioLib begin rc=%d(%s)", (int)state,
                 lora_radiolib_status_text(state));
        SLOGI("LoRa init failed: rc=%d (%s)", (int)state, lora_radiolib_status_text(state));
        lora_set_diag_step("radiolib_begin", state, g_lora.last_diag);
        lora_release_hardware();
        return;
    }

    (void)g_lora.radio->setCurrentLimit(140);
    (void)g_lora.radio->setDio2AsRfSwitch(true);

    g_lora.initialized         = true;
    g_lora.hw_ready            = true;
    g_lora.tx_mode             = false;
    g_lora.selected_tx_mode    = false;
    g_lora.tx_in_progress      = false;
    g_lora.pending_rx_after_tx = false;
    g_lora.tx_start_ms         = 0;
    g_lora.tx_timeout_ms       = cp0_lora_runtime_policy::TX_TIMEOUT_MS;
    g_lora.last_auto_tx_ms     = get_monotonic_ms();

    lora_set_diag_step("ready", 0, "LoRa init finished");
    SLOGI("LoRa: init done, auto enter RX");
    if (lora_stop_requested()) {
        lora_release_hardware();
        return;
    }
    lora_start_receive_mode();
}

void get_info(cp0_lora_info_t *info, bool drain_events)
{
    if (!info) return;
    memset(info, 0, sizeof(*info));
    info->initialized      = g_lora.initialized ? 1 : 0;
    info->hw_ready         = g_lora.hw_ready ? 1 : 0;
    info->tx_mode          = g_lora.tx_mode ? 1 : 0;
    info->tx_in_progress   = g_lora.tx_in_progress ? 1 : 0;
    info->has_sent_message = g_lora.has_sent_message ? 1 : 0;
    info->rx_event         = g_lora.rx_event ? 1 : 0;
    info->tx_event         = g_lora.tx_event ? 1 : 0;
    cp0_copy_cstr(info->spi_device, sizeof(info->spi_device), g_lora.spi.path());
    cp0_copy_cstr(info->last_rx, sizeof(info->last_rx), g_lora.last_rx);
    cp0_copy_cstr(info->last_tx, sizeof(info->last_tx), g_lora.last_tx);
    cp0_copy_cstr(info->diag, sizeof(info->diag), g_lora.last_diag);
    cp0_copy_cstr(info->probe_summary, sizeof(info->probe_summary), g_lora.probe_summary);
    cp0_copy_cstr(info->probe_display, sizeof(info->probe_display), g_lora.probe_display);
    cp0_copy_cstr(info->pi4io_status, sizeof(info->pi4io_status), cp0_lora_pi4io_controller::status());
    info->rssi = g_lora.last_rssi;
    info->snr  = g_lora.last_snr;
    if (drain_events) {
        g_lora.rx_event = false;
        g_lora.tx_event = false;
    }
}

bool initialize()
{
    if (!g_lora.initialized && !g_lora.hw_ready) lora_init_hardware();
    return g_lora.hw_ready;
}

void request_stop() noexcept
{
    g_stop_requested.store(true, std::memory_order_release);
}

void clear_stop() noexcept
{
    g_stop_requested.store(false, std::memory_order_release);
}

void poll()
{
    lora_poll_hardware();
}

bool send_text(const char *payload)
{
    return lora_send_text_packet(payload);
}

void start_receive()
{
    if (lora_stop_requested()) return;
    lora_start_receive_mode();
}

void set_tx_mode(bool enabled)
{
    lora_apply_mode(enabled);
}

void shutdown()
{
    lora_release_hardware();
}

}  // namespace cp0_lora_backend
