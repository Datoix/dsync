#include "audio_out.hpp"

#include <cinttypes>

#include "esp_check.h"
#include "esp_log.h"

namespace dsync::audio {
namespace {

constexpr char TAG[] = "audio_out";

constexpr size_t kRingBytes = 32 * 1024;
constexpr size_t kPrefetchBytes = 20 * 1024;
constexpr size_t kChunkBytes = 240 * 6;
constexpr TickType_t kStatsPeriodTicks = pdMS_TO_TICKS(1000);

}  // namespace

const char *Output::phase_str (I2sPhase p) {
    switch (p) {
    case I2sPhase::Closed:
        return "closed";
    case I2sPhase::Open:
        return "open";
    case I2sPhase::Running:
        return "running";
    }
    return "?";
}

const char *Output::ring_str (RingMode m) {
    switch (m) {
    case RingMode::Prefetching:
        return "prefetch";
    case RingMode::Processing:
        return "play";
    case RingMode::Dropping:
        return "drop";
    }
    return "?";
}

i2s_std_config_t Output::make_std_config (const dsync::board::DacPins &pins) {
    return i2s_std_config_t {
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
}

void Output::disable_i2s () {
    if (_tx_chan) {
        (void)i2s_channel_disable(_tx_chan.get());
    }
}

size_t Output::ring_bytes_used () const {
    size_t used = 0;
    if (_ringbuf) {
        vRingbufferGetInfo(_ringbuf.get(), nullptr, nullptr, nullptr, nullptr, &used);
    }
    return used;
}

void Output::reset_stream_stats () {
    _stats_tick = xTaskGetTickCount();
    _win_bytes_in = 0;
    _win_bytes_drop = 0;
    _win_underflows = 0;
}

void Output::note_write (size_t offered, size_t accepted) {
    if (accepted < offered) {
        _win_bytes_drop += static_cast<uint32_t>(offered - accepted);
    }
    _win_bytes_in += static_cast<uint32_t>(accepted);
    maybe_log_stream();
}

void Output::note_underflow () {
    ++_win_underflows;
    ++_total_underflows;
}

void Output::maybe_log_stream () {
    const TickType_t now = xTaskGetTickCount();
    if (_stats_tick == 0) {
        _stats_tick = now;
        return;
    }
    if ((now - _stats_tick) < kStatsPeriodTicks) {
        return;
    }

    const uint32_t expect = expect_pcm_bps();
    const uint32_t ring = static_cast<uint32_t>(ring_bytes_used());
    ESP_LOGI(
        TAG,
        "stream in=%" PRIu32 "B/s drop=%" PRIu32 "B/s expect=%" PRIu32
        "B/s (%" PRIu32 "Hz ch=%d) ring=%" PRIu32 "/%u uf=%" PRIu32
        "/%" PRIu32 " i2s=%s ring_mode=%s vol=n/a",
        _win_bytes_in,
        _win_bytes_drop,
        expect,
        _sample_rate_hz,
        _channel_count,
        ring,
        static_cast<unsigned>(kRingBytes),
        _win_underflows,
        _total_underflows,
        phase_str(_i2s_phase),
        ring_str(_ring_mode));

    _stats_tick = now;
    _win_bytes_in = 0;
    _win_bytes_drop = 0;
    _win_underflows = 0;
}

void Output::drain_to_i2s () {
    for (;;) {
        size_t item_size = 0;
        auto *data = static_cast<uint8_t *>(xRingbufferReceiveUpTo(
            _ringbuf.get(),
            &item_size,
            pdMS_TO_TICKS(20),
            kChunkBytes));

        if (item_size == 0) {
            note_underflow();
            _ring_mode = RingMode::Prefetching;
            return;
        }

        if (_i2s_phase == I2sPhase::Running) {
            size_t written = 0;
            (void)i2s_channel_write(
                _tx_chan.get(),
                data,
                item_size,
                &written,
                portMAX_DELAY);
        }
        vRingbufferReturnItem(_ringbuf.get(), data);
    }
}

void Output::write_task (void *arg) {
    auto *self = static_cast<Output *>(arg);

    for (;;) {
        if (xSemaphoreTake(self->_wake_sem.get(), portMAX_DELAY) != pdTRUE) {
            continue;
        }
        self->drain_to_i2s();
    }
}

esp_err_t Output::open () {
    if (_i2s_phase != I2sPhase::Closed) {
        return ESP_OK;
    }

    const auto &pins = dsync::board::kPins.dac;
    ESP_RETURN_ON_ERROR(pins.apply_mode(), TAG, "dac_mode");

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    const i2s_std_config_t std_cfg = make_std_config(pins);

    i2s_chan_handle_t raw = nullptr;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &raw, nullptr), TAG, "i2s_new_channel");
    _tx_chan.reset(raw);
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(_tx_chan.get(), &std_cfg), TAG, "i2s_init_std");

    _i2s_phase = I2sPhase::Open;
    ESP_LOGI(TAG, "i2s open");
    return ESP_OK;
}

void Output::close () {
    stop();
    _write_task.reset();
    _ringbuf.reset();
    _wake_sem.reset();

    if (_i2s_phase == I2sPhase::Open) {
        _tx_chan.reset();
        _i2s_phase = I2sPhase::Closed;
        ESP_LOGI(TAG, "i2s close");
    }
}

esp_err_t Output::ensure_wake_sem () {
    if (_wake_sem) {
        return ESP_OK;
    }
    _wake_sem.reset(xSemaphoreCreateBinary());
    return _wake_sem ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t Output::ensure_ring () {
    if (_ringbuf) {
        return ESP_OK;
    }
    _ringbuf.reset(xRingbufferCreate(kRingBytes, RINGBUF_TYPE_BYTEBUF));
    return _ringbuf ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t Output::ensure_writer_task () {
    if (_write_task) {
        return ESP_OK;
    }

    TaskHandle_t raw = nullptr;
    if (xTaskCreate(
            write_task,
            "i2s_wr",
            4 * 1024,
            this,
            configMAX_PRIORITIES - 3,
            &raw) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    _write_task.reset(raw);
    return ESP_OK;
}

esp_err_t Output::start () {
    if (_i2s_phase == I2sPhase::Running) {
        return ESP_OK;
    }
    if (_i2s_phase != I2sPhase::Open) {
        ESP_LOGE(TAG, "start: phase=%s", phase_str(_i2s_phase));
        return ESP_ERR_INVALID_STATE;
    }

    ESP_RETURN_ON_ERROR(i2s_channel_enable(_tx_chan.get()), TAG, "i2s_enable");
    _ring_mode = RingMode::Prefetching;
    reset_stream_stats();

    if (ensure_wake_sem() != ESP_OK || ensure_ring() != ESP_OK || ensure_writer_task() != ESP_OK) {
        disable_i2s();
        return ESP_ERR_NO_MEM;
    }

    _i2s_phase = I2sPhase::Running;
    ESP_LOGI(
        TAG,
        "i2s start expect=%" PRIu32 "B/s (%" PRIu32 "Hz ch=%d) vol=n/a",
        expect_pcm_bps(),
        _sample_rate_hz,
        _channel_count);
    return ESP_OK;
}

void Output::stop () {
    if (_i2s_phase != I2sPhase::Running || !_tx_chan) {
        return;
    }
    disable_i2s();
    _i2s_phase = I2sPhase::Open;
    ESP_LOGI(
        TAG,
        "i2s stop uf_total=%" PRIu32,
        _total_underflows);
}

esp_err_t Output::configure_pcm (uint32_t sample_rate_hz, int channel_count) {
    if (!_tx_chan) {
        return ESP_ERR_INVALID_STATE;
    }

    stop();

    _sample_rate_hz = sample_rate_hz;
    _channel_count = channel_count;

    const i2s_slot_mode_t slot =
        (channel_count == 1) ? I2S_SLOT_MODE_MONO : I2S_SLOT_MODE_STEREO;
    i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate_hz);
    i2s_std_slot_config_t slot_cfg =
        I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, slot);

    ESP_RETURN_ON_ERROR(
        i2s_channel_reconfig_std_clock(_tx_chan.get(), &clk_cfg),
        TAG,
        "reconfig clk");
    ESP_RETURN_ON_ERROR(
        i2s_channel_reconfig_std_slot(_tx_chan.get(), &slot_cfg),
        TAG,
        "reconfig slot");

    return ESP_OK;
}

void Output::on_drop_mode () {
    if (ring_bytes_used() <= kPrefetchBytes) {
        _ring_mode = RingMode::Processing;
    }
}

void Output::maybe_finish_prefetch () {
    if (_ring_mode != RingMode::Prefetching) {
        return;
    }
    if (ring_bytes_used() < kPrefetchBytes) {
        return;
    }
    _ring_mode = RingMode::Processing;
    (void)xSemaphoreGive(_wake_sem.get());
}

size_t Output::write (const uint8_t *data, size_t size) {
    if (!_ringbuf || !data || size == 0) {
        if (size > 0) {
            note_write(size, 0);
        }
        return 0;
    }

    if (_ring_mode == RingMode::Dropping) {
        on_drop_mode();
        note_write(size, 0);
        return 0;
    }

    if (xRingbufferSend(_ringbuf.get(), data, size, 0) != pdTRUE) {
        _ring_mode = RingMode::Dropping;
        note_write(size, 0);
        return 0;
    }

    maybe_finish_prefetch();
    note_write(size, size);
    return size;
}

}  // namespace dsync::audio
