#ifndef DSYNC_BOARD_HPP
#define DSYNC_BOARD_HPP

#include "esp_err.h"
#include "sdkconfig.h"

namespace dsync::board {

struct DacPins {
    int bck;
    int lrck;
    int dout;
    int fmt;
    int xsmt;
    int demp;
    int flt;

    /** Drive FMT/XSMT/DEMP/FLT for I2S + unmuted. */
    esp_err_t apply_mode () const;
};

struct LedPins {
    int gpio;
    int active_level;
};

struct Pins {
    DacPins dac;
    LedPins led;
};

inline constexpr Pins kPins {
    .dac = {
        .bck = CONFIG_DSYNC_I2S_BCK_GPIO,
        .lrck = CONFIG_DSYNC_I2S_LRCK_GPIO,
        .dout = CONFIG_DSYNC_I2S_DOUT_GPIO,
        .fmt = CONFIG_DSYNC_PCM5102_FMT_GPIO,
        .xsmt = CONFIG_DSYNC_PCM5102_XSMT_GPIO,
        .demp = CONFIG_DSYNC_PCM5102_DEMP_GPIO,
        .flt = CONFIG_DSYNC_PCM5102_FLT_GPIO,
    },
    .led = {
        .gpio = CONFIG_DSYNC_LED_GPIO,
        .active_level = CONFIG_DSYNC_LED_ACTIVE_LEVEL,
    },
};

}  // namespace dsync::board

#endif /* DSYNC_BOARD_HPP */
