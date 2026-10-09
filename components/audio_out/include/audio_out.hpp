#ifndef DSYNC_AUDIO_OUT_HPP
#define DSYNC_AUDIO_OUT_HPP

#include <cstddef>
#include <cstdint>

#include "board.hpp"
#include "driver/i2s_std.h"
#include "esp_err.h"
#include "idf_handles.hpp"

namespace dsync::audio {

/**
 * I2S TX → PCM5102A.
 * Owned by app_main; bt_sink holds a non-owning reference.
 *
 * PCM from A2DP is enqueued (non-blocking). A dedicated task ("i2s_wr")
 * drains the ringbuffer into I2S after a short prefetch.
 */
struct Output {
    esp_err_t open ();
    void close ();
    esp_err_t start ();
    void stop ();

    /** Match I2S clock/slots to the phone's SBC stream. */
    esp_err_t configure_pcm (uint32_t sample_rate_hz, int channel_count);

    /** Enqueue PCM (A2DP data callback). Returns bytes accepted. */
    size_t write (const uint8_t *data, size_t size);

private:
    enum class ChanState : uint8_t { Idle, Opened, Enabled };

    /** Prefetch until cushion, then Process; Drop if the ring is full. */
    enum class RingMode : uint8_t { Prefetching, Processing, Dropping };

    static void write_task (void *arg);

    void drain_to_i2s ();
    void disable_i2s ();
    size_t ring_bytes_used () const;
    void on_drop_mode ();
    void maybe_finish_prefetch ();

    esp_err_t ensure_wake_sem ();
    esp_err_t ensure_ring ();
    esp_err_t ensure_writer_task ();

    static i2s_std_config_t make_std_config (const dsync::board::DacPins &pins);

    handles::I2sChan _tx_chan;
    handles::Ringbuf _ringbuf;
    handles::Sem _wake_sem;
    handles::Task _write_task;

    ChanState _chan_st = ChanState::Idle;
    RingMode _ring_mode = RingMode::Prefetching;
};

}  // namespace dsync::audio

#endif /* DSYNC_AUDIO_OUT_HPP */
