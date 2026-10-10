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
 * Three tasks touch this object:
 *   A2DP data callback → push()        : enqueue only; never blocks, never logs
 *   "i2s_wr"           → writer_task() : drains the ring into I2S, logs stats
 *   "bt_work"          → open/start/stop/set_format
 *
 * Lifecycle:
 *   open()       allocate the channel + ring + writer task (idempotent)
 *   set_format() pick the I2S clock/slots, keeping the running state as it was
 *   start()      enable I2S and accept PCM      stop()  disable I2S, drop the ring
 *
 * Only two fields are shared between tasks, both atomic: `_running` (bt_work ↔
 * data callback) and `_bytes_dropped` (data callback ↔ writer). Everything else
 * is owned by a single task. The channel/ring/writer are allocated once and live
 * for the process, so nothing is ever freed under a running callback.
 */
struct Dac {
    esp_err_t open ();
    esp_err_t start ();
    void stop ();

    /** Match the I2S clock/slots to the negotiated stream; keeps running state. */
    esp_err_t set_format (uint32_t sample_rate_hz, int channels);

    /** Enqueue PCM from the A2DP data callback. Returns bytes accepted. */
    size_t push (const uint8_t *data, size_t size);

private:
    static void writer_task (void *arg);

    void drain ();
    void drop_ring ();
    void log_stats ();

    bool is_running () const {
        return _running.load(std::memory_order_acquire);
    }

    handles::I2sChan _chan;
    handles::Ringbuf _ring;
    handles::Task _writer;

    /** Set by bt_work, read by the data callback. */
    std::atomic<bool> _running {false};

    /** Written by the data callback, consumed by the writer. */
    std::atomic<uint32_t> _bytes_dropped {0};

    /** Writer-owned statistics. */
    uint32_t _bytes_written = 0;
    uint32_t _underflows = 0;
    TickType_t _last_log {};
};

}  // namespace dsync::audio

#endif /* DSYNC_AUDIO_OUT_HPP */
