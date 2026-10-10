#ifndef DSYNC_AUDIO_OUT_HPP
#define DSYNC_AUDIO_OUT_HPP

#include <cstddef>
#include <cstdint>

#include "board.hpp"
#include "driver/i2s_std.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "idf_handles.hpp"

namespace dsync::audio {

/**
 * I2S TX → PCM5102A.
 * Owned by app_main; bt_sink holds a non-owning reference.
 *
 * PCM from A2DP is enqueued (non-blocking). A dedicated task ("i2s_wr")
 * drains the ringbuffer into I2S after a short prefetch.
 *
 * Lifecycle: Closed → open → Open → start → Running;
 * configure_pcm leaves Open (caller start()s again).
 */
struct Output {
    esp_err_t open ();
    void close ();
    esp_err_t start ();
    void stop ();

    /** Match I2S clock/slots to the SBC stream. Leaves Open; call start() after. */
    esp_err_t configure_pcm (uint32_t sample_rate_hz, int channel_count);

    /** Enqueue PCM (A2DP data callback). Returns bytes accepted. */
    size_t write (const uint8_t *data, size_t size);

    uint32_t sample_rate_hz () const {
        return _sample_rate_hz;
    }

    int channel_count () const {
        return _channel_count;
    }

    /** Expected PCM byte rate for current format (16-bit samples). */
    uint32_t expect_pcm_bps () const {
        return _sample_rate_hz * static_cast<uint32_t>(_channel_count) * 2u;
    }

    /** Clear throughput counters (call when A2DP audio starts/stops). */
    void reset_stream_stats ();

private:
    enum class I2sPhase : uint8_t { Closed, Open, Running };
    enum class RingMode : uint8_t { Prefetching, Processing, Dropping };

    static void write_task (void *arg);

    void drain_to_i2s ();
    void disable_i2s ();
    size_t ring_bytes_used () const;
    void on_drop_mode ();
    void maybe_finish_prefetch ();
    void note_write (size_t offered, size_t accepted);
    void note_underflow ();
    void maybe_log_stream ();

    esp_err_t ensure_wake_sem ();
    esp_err_t ensure_ring ();
    esp_err_t ensure_writer_task ();

    static i2s_std_config_t make_std_config (const dsync::board::DacPins &pins);
    static const char *phase_str (I2sPhase p);
    static const char *ring_str (RingMode m);

    handles::I2sChan _tx_chan;
    handles::Ringbuf _ringbuf;
    handles::Sem _wake_sem;
    handles::Task _write_task;

    I2sPhase _i2s_phase = I2sPhase::Closed;
    RingMode _ring_mode = RingMode::Prefetching;

    uint32_t _sample_rate_hz = 44100;
    int _channel_count = 2;

    TickType_t _stats_tick = 0;
    uint32_t _win_bytes_in = 0;
    uint32_t _win_bytes_drop = 0;
    uint32_t _win_underflows = 0;
    uint32_t _total_underflows = 0;
};

}  // namespace dsync::audio

#endif /* DSYNC_AUDIO_OUT_HPP */
