#include "audio_out.hpp"

#include <cinttypes>

#include "esp_check.h"
#include "esp_log.h"
#include "sdkconfig.h"

namespace dsync::audio {
namespace {

constexpr char TAG[] = "audio_out";

/** Ring capacity between the A2DP producer and the writer (Kconfig). */
constexpr size_t kRingBytes = static_cast<size_t>(CONFIG_DSYNC_AUDIO_RING_KB) * 1024u;

/** Bytes per I2S write: one DMA descriptor burst, as in the IDF reference. */
constexpr size_t kChunkBytes = 240 * 6;

/** The writer waits this long for PCM before looping (also its idle tick). */
constexpr TickType_t kRingTimeout = pdMS_TO_TICKS(20);

/** At most one stats line per second. */
constexpr TickType_t kLogPeriod = pdMS_TO_TICKS(1000);

}  // namespace

esp_err_t Dac::open () {
    if (_chan) {
        return ESP_OK;  // allocated once; lives for the process
    }

    const auto &pins = dsync::board::kPins.dac;
    ESP_RETURN_ON_ERROR(pins.apply_mode(), TAG, "dac_mode");

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    const i2s_std_config_t std_cfg = i2s_std_config_t {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(44100),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = static_cast<gpio_num_t>(pins.bck),
            .ws = static_cast<gpio_num_t>(pins.lrck),
            .dout = static_cast<gpio_num_t>(pins.dout),
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    i2s_chan_handle_t chan = nullptr;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &chan, nullptr), TAG, "i2s_new_channel");
    _chan.reset(chan);
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(_chan.get(), &std_cfg), TAG, "i2s_init_std");

    _ring.reset(xRingbufferCreate(kRingBytes, RINGBUF_TYPE_BYTEBUF));
    if (!_ring) {
        return ESP_ERR_NO_MEM;
    }

    TaskHandle_t task = nullptr;
    if (xTaskCreate(
            writer_task,
            "i2s_wr",
            4 * 1024,
            this,
            configMAX_PRIORITIES - 3,
            &task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    _writer.reset(task);

    ESP_LOGI(TAG, "open ring=%uB", static_cast<unsigned>(kRingBytes));
    return ESP_OK;
}

esp_err_t Dac::start () {
    if (!_chan) {
        return ESP_ERR_INVALID_STATE;
    }
    if (is_running()) {
        return ESP_OK;
    }

    drop_ring();  // start from a clean buffer
    ESP_RETURN_ON_ERROR(i2s_channel_enable(_chan.get()), TAG, "i2s_enable");

    _bytes_written = 0;
    _underflows = 0;
    _running.store(true, std::memory_order_release);
    ESP_LOGI(TAG, "i2s start");
    return ESP_OK;
}

void Dac::stop () {
    if (!is_running()) {
        return;
    }

    _running.store(false, std::memory_order_release);
    (void)i2s_channel_disable(_chan.get());
    drop_ring();
    ESP_LOGI(TAG, "i2s stop uf=%" PRIu32, _underflows);
}

esp_err_t Dac::set_format (uint32_t sample_rate_hz, int channels) {
    if (!_chan) {
        return ESP_ERR_INVALID_STATE;
    }
    if (channels != 1 && channels != 2) {
        ESP_LOGE(TAG, "set_format: bad channels=%d", channels);
        return ESP_ERR_INVALID_ARG;
    }

    // The channel must be disabled to reconfigure; restore the running state after.
    const bool resume = is_running();
    stop();

    const i2s_slot_mode_t slot =
        (channels == 1) ? I2S_SLOT_MODE_MONO : I2S_SLOT_MODE_STEREO;
    i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate_hz);
    i2s_std_slot_config_t slot_cfg =
        I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, slot);

    ESP_RETURN_ON_ERROR(
        i2s_channel_reconfig_std_clock(_chan.get(), &clk_cfg),
        TAG,
        "reconfig clk");
    ESP_RETURN_ON_ERROR(
        i2s_channel_reconfig_std_slot(_chan.get(), &slot_cfg),
        TAG,
        "reconfig slot");

    ESP_LOGI(TAG, "format %" PRIu32 "Hz ch=%d", sample_rate_hz, channels);
    return resume ? start() : ESP_OK;
}

size_t Dac::push (const uint8_t *data, size_t size) {
    if (size == 0 || data == nullptr || !is_running()) {
        return 0;
    }

    if (xRingbufferSend(_ring.get(), data, size, 0) != pdTRUE) {
        _bytes_dropped.fetch_add(static_cast<uint32_t>(size), std::memory_order_relaxed);
        return 0;
    }
    return size;
}

void Dac::drain () {
    for (;;) {
        size_t size = 0;
        void *data = xRingbufferReceiveUpTo(_ring.get(), &size, kRingTimeout, kChunkBytes);

        if (size == 0) {
            if (is_running()) {
                ++_underflows;  // streaming but no PCM for a whole timeout
            }
            return;
        }

        if (is_running()) {
            size_t written = 0;
            (void)i2s_channel_write(_chan.get(), data, size, &written, portMAX_DELAY);
            _bytes_written += static_cast<uint32_t>(written);
        }
        vRingbufferReturnItem(_ring.get(), data);
    }
}

void Dac::writer_task (void *arg) {
    auto *self = static_cast<Dac *>(arg);

    for (;;) {
        self->drain();
        self->log_stats();
    }
}

/** Throw away queued PCM so a (re)start or stop never plays stale audio. */
void Dac::drop_ring () {
    if (!_ring) {
        return;
    }
    for (;;) {
        size_t size = 0;
        void *item = xRingbufferReceiveUpTo(_ring.get(), &size, 0, kRingBytes);
        if (size == 0) {
            return;
        }
        vRingbufferReturnItem(_ring.get(), item);
    }
}

void Dac::log_stats () {
    const TickType_t now = xTaskGetTickCount();
    if ((now - _last_log) < kLogPeriod) {
        return;
    }
    _last_log = now;

    const uint32_t dropped = _bytes_dropped.exchange(0, std::memory_order_relaxed);
    if (_bytes_written == 0 && dropped == 0 && _underflows == 0) {
        return;  // idle: no line
    }

    ESP_LOGI(
        TAG,
        "stream in=%" PRIu32 "B/s drop=%" PRIu32 "B/s uf=%" PRIu32 " running=%d",
        _bytes_written,
        dropped,
        _underflows,
        is_running() ? 1 : 0);

    _bytes_written = 0;
    _underflows = 0;
}

}  // namespace dsync::audio
