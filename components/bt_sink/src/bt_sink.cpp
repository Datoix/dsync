#include "bt_sink.hpp"

#include <cstring>

#include "esp_a2dp_api.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

namespace dsync::bt {
namespace {

constexpr char TAG[] = "bt_sink";

enum class WorkEvent : uint16_t {
    StackUp = 1,
    A2dpEvent,
};

struct WorkMsg {
    WorkEvent event = WorkEvent::StackUp;
    esp_a2d_cb_event_t a2d_event {};
    esp_a2d_cb_param_t a2d_param {};
};

}  // namespace

Sink *Sink::_active = nullptr;

Sink::~Sink () {
    if (_active == this) {
        _active = nullptr;
    }
}

void Sink::post_a2d_event (uint16_t event, const void *param, size_t param_size) {
    if (!_work_q) {
        return;
    }

    WorkMsg msg {};
    msg.event = WorkEvent::A2dpEvent;
    msg.a2d_event = static_cast<esp_a2d_cb_event_t>(event);
    if (param && param_size > 0) {
        const size_t n =
            param_size < sizeof(msg.a2d_param) ? param_size : sizeof(msg.a2d_param);
        std::memcpy(&msg.a2d_param, param, n);
    }
    (void)xQueueSend(_work_q.get(), &msg, 0);
}

void Sink::work_task (void *arg) {
    auto *self = static_cast<Sink *>(arg);
    WorkMsg msg {};

    for (;;) {
        if (xQueueReceive(self->_work_q.get(), &msg, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (msg.event == WorkEvent::StackUp) {
            self->on_stack_up();
            continue;
        }
        if (msg.event == WorkEvent::A2dpEvent) {
            self->handle_a2d_event(static_cast<uint16_t>(msg.a2d_event), &msg.a2d_param);
        }
    }
}

esp_err_t Sink::start_worker () {
    _work_q.reset(xQueueCreate(16, sizeof(WorkMsg)));
    if (!_work_q) {
        return ESP_ERR_NO_MEM;
    }

    TaskHandle_t raw = nullptr;
    if (xTaskCreate(work_task, "bt_work", 4096, this, 5, &raw) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    _work_task.reset(raw);
    return ESP_OK;
}

esp_err_t Sink::post_stack_up () {
    WorkMsg msg {};
    msg.event = WorkEvent::StackUp;
    if (xQueueSend(_work_q.get(), &msg, portMAX_DELAY) != pdTRUE) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t Sink::start () {
    ESP_RETURN_ON_ERROR(init_controller(), TAG, "controller");
    ESP_RETURN_ON_ERROR(start_worker(), TAG, "worker");
    _active = this;
    return post_stack_up();
}

}  // namespace dsync::bt
