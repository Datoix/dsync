#ifndef DSYNC_BT_SINK_HPP
#define DSYNC_BT_SINK_HPP

#include <cstddef>

#include "audio_out.hpp"
#include "esp_err.h"
#include "idf_handles.hpp"
#include "ui_leds.hpp"

namespace dsync::bt {

/**
 * Classic Bluetooth A2DP sink ("phone → speaker").
 *
 * Owns the BT worker task. Holds non-owning refs to audio/UI owned by app_main.
 * C stack callbacks reach this instance via active() (process-lifetime trampoline).
 */
struct Sink {
    Sink (dsync::audio::Output &audio, dsync::ui::Leds &leds)
        : _audio(audio)
        , _leds(leds) {}

    ~Sink ();

    Sink (const Sink &) = delete;
    Sink &operator= (const Sink &) = delete;

    /** NVS + Classic controller + Bluedroid + A2DP; become discoverable. */
    esp_err_t start ();

    /** Non-owning pointer for C BT callbacks (set in start()). */
    static Sink *active () {
        return _active;
    }

    dsync::audio::Output &audio () {
        return _audio;
    }

    /** Post an A2DP control event from the stack callback (non-blocking). */
    void post_a2d_event (uint16_t event, const void *param, size_t param_size);

private:
    static void work_task (void *arg);

    esp_err_t init_controller ();
    void on_stack_up ();
    void handle_a2d_event (uint16_t event, void *param);

    dsync::audio::Output &_audio;
    dsync::ui::Leds &_leds;
    handles::Queue _work_q;
    handles::Task _work_task;

    static Sink *_active;
};

}  // namespace dsync::bt

#endif /* DSYNC_BT_SINK_HPP */
