#ifndef DSYNC_BOARD_HPP
#define DSYNC_BOARD_HPP

#include <cstdint>

namespace dsync::board {

struct I2sPins {
    int bck;
    int lrck;
    int dout;
};

struct LedPins {
    int gpio;
    int active_level;
};

I2sPins i2s_pins ();
LedPins led_pins ();

}  // namespace dsync::board

#endif /* DSYNC_BOARD_HPP */
