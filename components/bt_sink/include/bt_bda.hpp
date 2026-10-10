#ifndef DSYNC_BT_BDA_HPP
#define DSYNC_BT_BDA_HPP

#include <cstddef>
#include <cstdio>

namespace dsync::bt {

/**
 * Render a 6-byte Bluetooth device address as "aa:bb:cc:dd:ee:ff".
 *
 * Shared by the controller and A2DP units (both log peer addresses).
 * Returns `str`, or nullptr if the buffer is missing or too small (needs 18).
 */
inline char *bda2str (const uint8_t *bda, char *str, size_t size) {
    if (bda == nullptr || str == nullptr || size < 18) {
        return nullptr;
    }
    std::snprintf(
        str,
        size,
        "%02x:%02x:%02x:%02x:%02x:%02x",
        bda[0],
        bda[1],
        bda[2],
        bda[3],
        bda[4],
        bda[5]);
    return str;
}

}  // namespace dsync::bt

#endif /* DSYNC_BT_BDA_HPP */
