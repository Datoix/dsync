#ifndef DSYNC_IDF_HANDLES_HPP
#define DSYNC_IDF_HANDLES_HPP

/**
 * Owning wrappers for ESP-IDF / FreeRTOS C handles.
 * unique_ptr needs a custom deleter — default delete is wrong for these.
 */

#include <memory>
#include <type_traits>

#include "driver/i2s_std.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/ringbuf.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace dsync::handles {

template <typename Handle, auto DeleteFn>
struct Deleter {
    void operator() (Handle h) const {
        if (h) {
            DeleteFn(h);
        }
    }
};

inline void delete_i2s_chan (i2s_chan_handle_t h) {
    (void)i2s_del_channel(h);
}

inline void delete_ringbuf (RingbufHandle_t h) {
    vRingbufferDelete(h);
}

inline void delete_sem (SemaphoreHandle_t h) {
    vSemaphoreDelete(h);
}

inline void delete_queue (QueueHandle_t h) {
    vQueueDelete(h);
}

inline void delete_task (TaskHandle_t h) {
    vTaskDelete(h);
}

inline void delete_timer (esp_timer_handle_t h) {
    (void)esp_timer_stop(h);
    (void)esp_timer_delete(h);
}

template <typename Handle, auto DeleteFn>
using Ptr = std::unique_ptr<std::remove_pointer_t<Handle>, Deleter<Handle, DeleteFn>>;

using I2sChan = Ptr<i2s_chan_handle_t, delete_i2s_chan>;
using Ringbuf = Ptr<RingbufHandle_t, delete_ringbuf>;
using Sem = Ptr<SemaphoreHandle_t, delete_sem>;
using Queue = Ptr<QueueHandle_t, delete_queue>;
using Task = Ptr<TaskHandle_t, delete_task>;
using Timer = Ptr<esp_timer_handle_t, delete_timer>;

}  // namespace dsync::handles

#endif /* DSYNC_IDF_HANDLES_HPP */
