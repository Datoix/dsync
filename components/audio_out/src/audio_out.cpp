#include "audio_out.hpp"

#include <cinttypes>

#include "esp_check.h"
#include "esp_log.h"
#include "sdkconfig.h"

namespace dsync::audio {
namespace {

constexpr char TAG[] = "audio_out";

/** Ring capacity and start-playback cushion (Kconfig in Kconfig.projbuild). */
constexpr size_t kRingBytes = static_cast<size_t>(CONFIG_DSYNC_AUDIO_RING_KB) * 1024u;
constexpr size_t kPrefetchBytes = static_cast<size_t>(CONFIG_DSYNC_AUDIO_PREFETCH_KB) * 1024u;

/** Bytes per I2S write (matches the IDF reference: one DMA descriptor burst). */
constexpr size_t kChunkBytes = 240 * 6;

/** How long the writer waits for more PCM before declaring an underflow. */
constexpr TickType_t kRingRxTimeout = pdMS_TO_TICKS(20);

/** Stats are emitted at most once per second, and only by the writer task. */
constexpr TickType_t kStatsPeriod = pdMS_TO_TICKS(1000);

}  // namespace

const char *Output::phase_str (I2sPhase p) {
    switch (p) {
    case I2sPhase::Idle:
        return "idle";
    case I2sPhase::Ready:
        return "ready";
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

/** Drop everything queued so a reconnect (or a new format) starts clean. */
void Output::clear_ring () {
    if (!_ringbuf) {
        return;
    }
    for (;;) {
        size_t size = 0;
        void *item = xRingbufferReceiveUpTo(_ringbuf.get(), &size, 0, kRingBytes);
        if (item == nullptr || size == 0) {
            return;
        }
        vRingbufferReturnItem(_ringbuf.get(), item);
    }
}

void Output::reset_stream_stats () {
    _win_bytes_in.store(0, std::memory_order_relaxed);
    _win_bytes_drop.store(0, std::memory_order_relaxed);
    _win_underflows.store(0, std::memory_order_relaxed);
    // _stats_tick belongs to the writer task; leave it alone here.
}

void Output::drain_to_i2s () {
    for (;;) {
        size_t size = 0;
        void *data = xRingbufferReceiveUpTo(_ringbuf.get(), &size, kRingRxTimeout, kChunkBytes);

        if (size == 0) {
            // Producer too slow → underrun. Rebuild the cushion before playing again.
            _win_underflows.fetch_add(1, std::memory_order_relaxed);
            _total_underflows.fetch_add(1, std::memory_order_relaxed);
            _ring_mode.store(RingMode::Prefetching, std::memory_order_relaxed);
            return;
        }

        // stop() may disable the channel mid-drain; skip the write in that case.
        if (_i2s_phase.load(std::memory_order_relaxed) == I2sPhase::Running) {
            size_t written = 0;
            (void)i2s_channel_write(_tx_chan.get(), data, size, &written, portMAX_DELAY);
        }
        vRingbufferReturnItem(_ringbuf.get(), data);
    }
}

void Output::write_task (void *arg) {
    auto *self = static_cast<Output *>(arg);

    for (;;) {
        // Wake on new PCM, or every kStatsPeriod so stats are emitted from THIS
        // task and never from the A2DP data callback.
        if (xSemaphoreTake(self->_wake_sem.get(), kStatsPeriod) == pdTRUE) {
            self->drain_to_i2s();
        }
        self->maybe_log_stream();
    }
}

/** Emit one throughput line per second while the pipeline is active. */
void Output::maybe_log_stream () {
    const TickType_t now = xTaskGetTickCount();
    if (_stats_tick == 0) {
        _stats_tick = now;
        return;
    }
    if ((now - _stats_tick) < kStatsPeriod) {
        return;
    }

    const uint32_t in = _win_bytes_in.exchange(0, std::memory_order_relaxed);
    const uint32_t drop = _win_bytes_drop.exchange(0, std::memory_order_relaxed);
    const uint32_t uf = _win_underflows.exchange(0, std::memory_order_relaxed);
    _stats_tick = now;

    if (in == 0 && drop == 0 && uf == 0) {
        return;  // idle: don't spam when nothing is streaming
    }

    ESP_LOGI(
        TAG,
        "stream in=%" PRIu32 "B/s drop=%" PRIu32 "B/s expect=%" PRIu32
        "B/s (%" PRIu32 "Hz ch=%d) ring=%u/%u uf=%" PRIu32 "/%" PRIu32
        " i2s=%s ring_mode=%s",
        in,
        drop,
        expect_pcm_bps(),
        sample_rate_hz(),
        channel_count(),
        static_cast<unsigned>(ring_bytes_used()),
        static_cast<unsigned>(kRingBytes),
        uf,
        _total_underflows.load(std::memory_order_relaxed),
        phase_str(_i2s_phase.load(std::memory_order_relaxed)),
        ring_str(_ring_mode.load(std::memory_order_relaxed)));
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

esp_err_t Output::open () {
    if (_i2s_phase.load(std::memory_order_relaxed) != I2sPhase::Idle) {
        return ESP_OK;  // already allocated; the channel is process-lifetime
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

    ESP_RETURN_ON_ERROR(ensure_wake_sem(), TAG, "wake_sem");
    ESP_RETURN_ON_ERROR(ensure_ring(), TAG, "ring");
    ESP_RETURN_ON_ERROR(ensure_writer_task(), TAG, "writer_task");

    _i2s_phase.store(I2sPhase::Ready, std::memory_order_relaxed);
    ESP_LOGI(
        TAG,
        "i2s open ring=%uB prefetch=%uB",
        static_cast<unsigned>(kRingBytes),
        static_cast<unsigned>(kPrefetchBytes));
    return ESP_OK;
}

esp_err_t Output::start () {
    const I2sPhase phase = _i2s_phase.load(std::memory_order_relaxed);
    if (phase == I2sPhase::Running) {
        return ESP_OK;
    }
    if (phase != I2sPhase::Ready) {
        ESP_LOGE(TAG, "start: state=%s (open() first)", phase_str(phase));
        return ESP_ERR_INVALID_STATE;
    }

    clear_ring();
    ESP_RETURN_ON_ERROR(i2s_channel_enable(_tx_chan.get()), TAG, "i2s_enable");

    _ring_mode.store(RingMode::Prefetching, std::memory_order_relaxed);
    reset_stream_stats();
    _i2s_phase.store(I2sPhase::Running, std::memory_order_relaxed);
    _accepting.store(true, std::memory_order_release);

    ESP_LOGI(
        TAG,
        "i2s start expect=%" PRIu32 "B/s (%" PRIu32 "Hz ch=%d)",
        expect_pcm_bps(),
        sample_rate_hz(),
        channel_count());
    return ESP_OK;
}

void Output::stop () {
    if (_i2s_phase.load(std::memory_order_relaxed) != I2sPhase::Running) {
        return;
    }

    // Stop accepting first so the data callback can't enqueue into a ring we clear.
    _accepting.store(false, std::memory_order_release);
    disable_i2s();
    clear_ring();
    _i2s_phase.store(I2sPhase::Ready, std::memory_order_relaxed);

    ESP_LOGI(TAG, "i2s stop uf_total=%" PRIu32, _total_underflows.load(std::memory_order_relaxed));
}

esp_err_t Output::configure_pcm (uint32_t sample_rate_hz, int channel_count) {
    if (!_tx_chan) {
        return ESP_ERR_INVALID_STATE;
    }
    if (channel_count < 1 || channel_count > 2) {
        return ESP_ERR_INVALID_ARG;
    }

    stop();  // must be disabled to reconfigure; leaves Ready

    _sample_rate_hz.store(sample_rate_hz, std::memory_order_relaxed);
    _channel_count.store(channel_count, std::memory_order_relaxed);

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

    ESP_LOGI(
        TAG,
        "pcm %" PRIu32 "Hz ch=%d expect=%" PRIu32 "B/s",
        sample_rate_hz,
        channel_count,
        expect_pcm_bps());
    return ESP_OK;
}

size_t Output::write (const uint8_t *data, size_t size) {
    if (size == 0) {
        return 0;
    }

    // _accepting gates against stop(); the ring itself is process-lifetime.
    if (!_accepting.load(std::memory_order_acquire) || !_ringbuf || data == nullptr) {
        _win_bytes_drop.fetch_add(static_cast<uint32_t>(size), std::memory_order_relaxed);
        return 0;
    }

    if (_ring_mode.load(std::memory_order_relaxed) == RingMode::Dropping) {
        // Ring was full: resume only once the writer has drained below the cushion.
        if (ring_bytes_used() <= kPrefetchBytes) {
            _ring_mode.store(RingMode::Processing, std::memory_order_relaxed);
        }
        _win_bytes_drop.fetch_add(static_cast<uint32_t>(size), std::memory_order_relaxed);
        return 0;
    }

    if (xRingbufferSend(_ringbuf.get(), data, size, 0) != pdTRUE) {
        _ring_mode.store(RingMode::Dropping, std::memory_order_relaxed);
        _win_bytes_drop.fetch_add(static_cast<uint32_t>(size), std::memory_order_relaxed);
        return 0;
    }

    _win_bytes_in.fetch_add(static_cast<uint32_t>(size), std::memory_order_relaxed);

    // Wake the writer once the cushion is filled (Prefetching → Processing).
    if (_ring_mode.load(std::memory_order_relaxed) == RingMode::Prefetching &&
        ring_bytes_used() >= kPrefetchBytes) {
        _ring_mode.store(RingMode::Processing, std::memory_order_relaxed);
        (void)xSemaphoreGive(_wake_sem.get());
    }
    return size;
}

}  // namespace dsync::audio
