#ifndef DSYNC_AUDIO_OUT_HPP
#define DSYNC_AUDIO_OUT_HPP

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "board.hpp"
#include "driver/i2s_std.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "idf_handles.hpp"

namespace dsync::audio {

/**
 * I2S TX → PCM5102A, fed from the A2DP data callback through a ring buffer.
 * Owned by app_main; bt_sink holds a non-owning reference.
 *
 * Threading model
 * ---------------
 *   A2DP data callback task → write()         : enqueue only (never blocks, never logs)
 *   "i2s_wr" task           → drain_to_i2s()  : pops the ring, writes I2S, emits stats
 *   "bt_work" task          → open/start/stop/configure_pcm : owns the stream state
 *
 * All shared scalars are std::atomic so no task can observe a torn value; the
 * ring buffer carries its own locking. The I2S channel, ring, semaphore and
 * writer task are created once in open() and live for the process lifetime, so
 * teardown can never free memory the data callback or writer task still touches.
 *
 * Stream states
 * -------------
 *   Idle    → nothing allocated yet (only before the first connection)
 *   Ready   → channel allocated and disabled; the PCM format may be reconfigured
 *   Running → channel enabled; write() accepts PCM
 *
 * Ring modes (how the ring is driven)
 * -----------------------------------
 *   Prefetching → fill to the cushion before the writer starts (absorbs jitter)
 *   Processing  → writer drains normally
 *   Dropping    → ring full; drop incoming PCM until the writer catches up
 */
struct Output {
    /** Allocate I2S + ring + writer task (idempotent). Leaves state Ready. */
    esp_err_t open ();

    /** Enable I2S and start accepting PCM. Leaves state Running. */
    esp_err_t start ();

    /** Stop accepting PCM, disable I2S and drop queued audio. Leaves state Ready. */
    void stop ();

    /** Match the I2S clock/slots to the negotiated SBC stream. Leaves state Ready. */
    esp_err_t configure_pcm (uint32_t sample_rate_hz, int channel_count);

    /** Enqueue PCM from the A2DP data callback. Returns bytes accepted. */
    size_t write (const uint8_t *data, size_t size);

    uint32_t sample_rate_hz () const {
        return _sample_rate_hz.load(std::memory_order_relaxed);
    }

    int channel_count () const {
        return _channel_count.load(std::memory_order_relaxed);
    }

    /** Expected PCM byte rate for the current format (16-bit samples). */
    uint32_t expect_pcm_bps () const {
        return sample_rate_hz() * static_cast<uint32_t>(channel_count()) * 2u;
    }

    /** Zero the throughput counters (call when A2DP audio starts/stops). */
    void reset_stream_stats ();

private:
    enum class I2sPhase : uint8_t { Idle, Ready, Running };
    enum class RingMode : uint8_t { Prefetching, Processing, Dropping };

    static void write_task (void *arg);

    void drain_to_i2s ();
    void clear_ring ();
    void disable_i2s ();
    size_t ring_bytes_used () const;
    void maybe_log_stream ();

    esp_err_t ensure_wake_sem ();
    esp_err_t ensure_ring ();
    esp_err_t ensure_writer_task ();

    static i2s_std_config_t make_std_config (const dsync::board::DacPins &pins);
    static const char *phase_str (I2sPhase p);
    static const char *ring_str (RingMode m);

    // Process-lifetime resources (see the threading note above).
    handles::I2sChan _tx_chan;
    handles::Ringbuf _ringbuf;
    handles::Sem _wake_sem;
    handles::Task _write_task;

    std::atomic<I2sPhase> _i2s_phase {I2sPhase::Idle};
    std::atomic<RingMode> _ring_mode {RingMode::Prefetching};
    /** True only between start() and stop(); gates write() against teardown. */
    std::atomic<bool> _accepting {false};

    std::atomic<uint32_t> _sample_rate_hz {44100};
    std::atomic<int> _channel_count {2};

    /** Throughput counters: written by the producer, consumed by the writer task. */
    std::atomic<uint32_t> _win_bytes_in {0};
    std::atomic<uint32_t> _win_bytes_drop {0};
    std::atomic<uint32_t> _win_underflows {0};
    std::atomic<uint32_t> _total_underflows {0};

    /** Log-window clock; owned exclusively by the writer task. */
    TickType_t _stats_tick {};
};

}  // namespace dsync::audio

#endif /* DSYNC_AUDIO_OUT_HPP */
