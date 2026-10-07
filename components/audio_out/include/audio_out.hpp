#ifndef DSYNC_AUDIO_OUT_HPP
#define DSYNC_AUDIO_OUT_HPP

#include <cstddef>
#include <cstdint>

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

    handles::I2sChan _tx_chan;
    handles::Ringbuf _ringbuf;
    handles::Sem _wake_sem;   // binary: wake writer after prefetch
    handles::Task _write_task;

    ChanState _chan_st = ChanState::Idle;
    RingMode _ring_mode = RingMode::Prefetching;
};

}  // namespace dsync::audio

#endif /* DSYNC_AUDIO_OUT_HPP */
