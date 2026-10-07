#ifndef DSYNC_UI_LEDS_HPP
#define DSYNC_UI_LEDS_HPP

#include <atomic>
#include <cstdint>

#include "driver/gpio.h"
#include "esp_err.h"
#include "idf_handles.hpp"

namespace dsync::ui {

enum class Status : uint8_t {
    Idle = 0,
    Discoverable,
    Connected,
    Playing,
};

/**
 * Onboard (or PCB) status LED.
 * Patterns: idle pulse, slow blink = discoverable, solid = connected, fast = playing.
 * Status is atomic so BT worker and the timer task need no mutex.
 */
struct Leds {
    esp_err_t init ();
    void set_status (Status status);

private:
    static void timer_cb (void *arg);

    void apply_level (bool on);
    bool level_for_tick (Status st, uint32_t tick) const;
    void init_gpio ();
    esp_err_t start_timer ();

    gpio_num_t _gpio = GPIO_NUM_NC;
    int _active_level = 1;
    std::atomic<Status> _status {Status::Idle};
    std::atomic<uint32_t> _tick {0};
    handles::Timer _timer;
};

}  // namespace dsync::ui

#endif /* DSYNC_UI_LEDS_HPP */
