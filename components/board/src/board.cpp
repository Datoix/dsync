#include "board.hpp"

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"

namespace dsync::board {
namespace {

constexpr char TAG[] = "board";

/** Fixed levels for I2S stereo out (not Kconfig — wrong levels = silence/garbled). */
constexpr int kFmtLevel = 0;   // L = I2S
constexpr int kXsmtLevel = 1;  // H = unmute
constexpr int kDempLevel = 0;  // L = de-emphasis off
constexpr int kFltLevel = 0;   // L = normal latency filter

esp_err_t drive_out (int gpio, int level) {
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << static_cast<unsigned>(gpio),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&cfg), TAG, "gpio_config %d", gpio);
    return gpio_set_level(static_cast<gpio_num_t>(gpio), level);
}

}  // namespace

esp_err_t DacPins::apply_mode () const {
    ESP_RETURN_ON_ERROR(drive_out(fmt, kFmtLevel), TAG, "FMT");
    ESP_RETURN_ON_ERROR(drive_out(xsmt, kXsmtLevel), TAG, "XSMT");
    ESP_RETURN_ON_ERROR(drive_out(demp, kDempLevel), TAG, "DEMP");
    ESP_RETURN_ON_ERROR(drive_out(flt, kFltLevel), TAG, "FLT");
    ESP_LOGI(
        TAG,
        "DAC mode FMT=%d:0 XSMT=%d:1 DEMP=%d:0 FLT=%d:0",
        fmt,
        xsmt,
        demp,
        flt);
    return ESP_OK;
}

}  // namespace dsync::board
