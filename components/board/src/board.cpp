#include "board.hpp"

#include "sdkconfig.h"

namespace dsync::board {

I2sPins i2s_pins () {
    return I2sPins{
        .bck = CONFIG_DSYNC_I2S_BCK_GPIO,
        .lrck = CONFIG_DSYNC_I2S_LRCK_GPIO,
        .dout = CONFIG_DSYNC_I2S_DOUT_GPIO,
    };
}

LedPins led_pins () {
    return LedPins{
        .gpio = CONFIG_DSYNC_LED_GPIO,
        .active_level = CONFIG_DSYNC_LED_ACTIVE_LEVEL,
    };
}

}  // namespace dsync::board
