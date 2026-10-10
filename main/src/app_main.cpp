#include "audio_out.hpp"
#include "bt_sink.hpp"
#include "esp_log.h"
#include "ui_leds.hpp"

namespace {
constexpr char TAG[] = "main";
}

extern "C" void app_main (void) {
    ESP_LOGI(TAG, "dsync 0.1 starting");

    // Process-lifetime owners; Sink only holds references.
    static dsync::ui::Leds leds;
    static dsync::audio::Dac dac;
    static dsync::bt::Sink sink(dac, leds);

    ESP_ERROR_CHECK(leds.init());
    ESP_ERROR_CHECK(sink.start());
}
