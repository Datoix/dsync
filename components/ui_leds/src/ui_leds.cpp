#include "ui_leds.hpp"

#include "board.hpp"
#include "esp_log.h"

namespace dsync::ui {
namespace {

constexpr char TAG[] = "ui_leds";
/** Periodic blink base tick (ms). Patterns are multiples of this. */
constexpr uint64_t kTickUs = 50 * 1000ULL;

}  // namespace

void Leds::apply_level (bool on) {
    const int level = on ? _active_level : (1 - _active_level);
    gpio_set_level(_gpio, level);
}

bool Leds::level_for_tick (Status st, uint32_t tick) const {
    switch (st) {
    case Status::Connected:
        return true;
    case Status::Playing:
        // 100 ms on / 100 ms off → 2 ticks each
        return ((tick / 2) % 2) == 0;
    case Status::Discoverable:
        // 500 ms on / 500 ms off → 10 ticks each
        return ((tick / 10) % 2) == 0;
    case Status::Idle:
    default:
        // short pulse every 2 s → on for 1 of 40 ticks
        return (tick % 40) == 0;
    }
}

void Leds::timer_cb (void *arg) {
    auto *self = static_cast<Leds *>(arg);
    if (!self) {
        return;
    }

    const Status st = self->_status.load(std::memory_order_relaxed);
    const uint32_t tick = self->_tick.fetch_add(1, std::memory_order_relaxed);
    self->apply_level(self->level_for_tick(st, tick));
}

esp_err_t Leds::init () {
    const auto pins = dsync::board::led_pins();
    _gpio = static_cast<gpio_num_t>(pins.gpio);
    _active_level = pins.active_level;

    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << static_cast<unsigned>(_gpio),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));
    apply_level(false);

    esp_timer_handle_t raw = nullptr;
    const esp_timer_create_args_t targs = {
        .callback = &Leds::timer_cb,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "led_tick",
        .skip_unhandled_events = true,
    };
    ESP_ERROR_CHECK(esp_timer_create(&targs, &raw));
    _timer.reset(raw);

    _status.store(Status::Idle, std::memory_order_relaxed);
    _tick.store(0, std::memory_order_relaxed);
    ESP_ERROR_CHECK(esp_timer_start_periodic(_timer.get(), kTickUs));

    ESP_LOGI(TAG, "LED on GPIO %d", static_cast<int>(_gpio));
    return ESP_OK;
}

void Leds::set_status (Status status) {
    _status.store(status, std::memory_order_relaxed);
    _tick.store(0, std::memory_order_relaxed);
}

}  // namespace dsync::ui
