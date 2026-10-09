#include "ui_leds.hpp"

#include "board.hpp"
#include "esp_log.h"

namespace dsync::ui {
namespace {

constexpr char TAG[] = "ui_leds";

/** Periodic blink base period (microseconds). Patterns are multiples of this. */
constexpr uint64_t kTickUs = 50 * 1000ULL;

/** Playing: 100 ms on / 100 ms off → 2 ticks each → period 4. */
constexpr uint32_t kPlayingHalfTicks = 2;
/** Discoverable: 500 ms on / 500 ms off → 10 ticks each. */
constexpr uint32_t kDiscoverableHalfTicks = 10;
/** Idle: short pulse every 2 s → 1 of 40 ticks. */
constexpr uint32_t kIdlePeriodTicks = 40;

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
        return ((tick / kPlayingHalfTicks) % 2) == 0;
    case Status::Discoverable:
        return ((tick / kDiscoverableHalfTicks) % 2) == 0;
    case Status::Idle:
    default:
        return (tick % kIdlePeriodTicks) == 0;
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

void Leds::init_gpio () {
    const auto &pins = dsync::board::kPins.led;
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
}

esp_err_t Leds::start_timer () {
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
    return esp_timer_start_periodic(_timer.get(), kTickUs);
}

esp_err_t Leds::init () {
    init_gpio();

    _status.store(Status::Idle, std::memory_order_relaxed);
    _tick.store(0, std::memory_order_relaxed);
    ESP_ERROR_CHECK(start_timer());

    ESP_LOGI(TAG, "LED on GPIO %d", static_cast<int>(_gpio));
    return ESP_OK;
}

void Leds::set_status (Status status) {
    _status.store(status, std::memory_order_relaxed);
    _tick.store(0, std::memory_order_relaxed);
}

}  // namespace dsync::ui
