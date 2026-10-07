#include "bt_sink.hpp"

#include <cinttypes>
#include <cstdio>
#include <cstring>

#include "esp_a2dp_api.h"
#include "esp_bt.h"
#include "esp_bt_device.h"
#include "esp_bt_main.h"
#include "esp_check.h"
#include "esp_gap_bt_api.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

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

char *bda2str (const uint8_t *bda, char *str, size_t size) {
    if (!bda || !str || size < 18) {
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

uint32_t sbc_sample_rate (const esp_a2d_mcc_t &mcc) {
    if (mcc.cie.sbc_info.samp_freq & ESP_A2D_SBC_CIE_SF_48K) {
        return 48000;
    }
    if (mcc.cie.sbc_info.samp_freq & ESP_A2D_SBC_CIE_SF_44K) {
        return 44100;
    }
    if (mcc.cie.sbc_info.samp_freq & ESP_A2D_SBC_CIE_SF_32K) {
        return 32000;
    }
    return 16000;
}

int sbc_channels (const esp_a2d_mcc_t &mcc) {
    if (mcc.cie.sbc_info.ch_mode & ESP_A2D_SBC_CIE_CH_MODE_MONO) {
        return 1;
    }
    return 2;
}

void gap_cb (esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param) {
    if (!Sink::active()) {
        return;
    }

    switch (event) {
    case ESP_BT_GAP_AUTH_CMPL_EVT:
        if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
            ESP_LOGI(TAG, "auth ok: %s", param->auth_cmpl.device_name);
        } else {
            ESP_LOGE(TAG, "auth fail: %d", param->auth_cmpl.stat);
        }
        break;
#if CONFIG_DSYNC_BT_SSP_ENABLED
    case ESP_BT_GAP_CFM_REQ_EVT:
        ESP_LOGI(TAG, "SSP confirm: %" PRIu32, param->cfm_req.num_val);
        esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
        break;
    case ESP_BT_GAP_KEY_NOTIF_EVT:
        ESP_LOGI(TAG, "SSP passkey: %" PRIu32, param->key_notif.passkey);
        break;
    case ESP_BT_GAP_KEY_REQ_EVT:
        ESP_LOGI(TAG, "SSP passkey requested");
        break;
#endif
    default:
        break;
    }
}

void a2d_cb (esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param) {
    auto *self = Sink::active();
    if (!self) {
        return;
    }
    self->post_a2d_event(
        static_cast<uint16_t>(event),
        param,
        param ? sizeof(*param) : 0);
}

void a2d_data_cb (const uint8_t *data, uint32_t len) {
    auto *self = Sink::active();
    if (!self) {
        return;
    }
    (void)self->audio().write(data, len);
}

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
        const size_t n = param_size < sizeof(msg.a2d_param) ? param_size : sizeof(msg.a2d_param);
        std::memcpy(&msg.a2d_param, param, n);
    }
    (void)xQueueSend(_work_q.get(), &msg, 0);
}

void Sink::handle_a2d_event (uint16_t event, void *param) {
    auto *a2d = static_cast<esp_a2d_cb_param_t *>(param);
    static const char *conn_str[] = {"Disconnected", "Connecting", "Connected", "Disconnecting"};
    static const char *audio_str[] = {"Suspended", "Started"};

    switch (static_cast<esp_a2d_cb_event_t>(event)) {
    case ESP_A2D_CONNECTION_STATE_EVT: {
        char bda_str[18] = {};
        ESP_LOGI(
            TAG,
            "A2DP %s [%s]",
            conn_str[a2d->conn_stat.state],
            bda2str(a2d->conn_stat.remote_bda, bda_str, sizeof(bda_str)));

        if (a2d->conn_stat.state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
            esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
            _audio.stop();
            _audio.close();
            _leds.set_status(dsync::ui::Status::Discoverable);
        } else if (a2d->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTING) {
            (void)_audio.open();
        } else if (a2d->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
            esp_bt_gap_set_scan_mode(ESP_BT_NON_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
            (void)_audio.start();
            _leds.set_status(dsync::ui::Status::Connected);
        }
        break;
    }
    case ESP_A2D_AUDIO_STATE_EVT:
        ESP_LOGI(TAG, "A2DP audio %s", audio_str[a2d->audio_stat.state]);
        if (a2d->audio_stat.state == ESP_A2D_AUDIO_STATE_STARTED) {
            _leds.set_status(dsync::ui::Status::Playing);
        } else {
            _leds.set_status(dsync::ui::Status::Connected);
        }
        break;
    case ESP_A2D_AUDIO_CFG_EVT:
        if (a2d->audio_cfg.mcc.type == ESP_A2D_MCT_SBC) {
            const uint32_t rate = sbc_sample_rate(a2d->audio_cfg.mcc);
            const int ch = sbc_channels(a2d->audio_cfg.mcc);
            (void)_audio.configure_pcm(rate, ch);
            (void)_audio.start();
        }
        break;
    case ESP_A2D_PROF_STATE_EVT:
    case ESP_A2D_SNK_PSC_CFG_EVT:
    case ESP_A2D_SNK_SET_DELAY_VALUE_EVT:
    case ESP_A2D_SNK_GET_DELAY_VALUE_EVT:
        break;
    default:
        ESP_LOGD(TAG, "A2DP event %d", static_cast<int>(event));
        break;
    }
}

void Sink::on_stack_up () {
    ESP_ERROR_CHECK(esp_bt_gap_set_device_name(CONFIG_DSYNC_BT_DEVICE_NAME));
    ESP_ERROR_CHECK(esp_bt_gap_register_callback(gap_cb));
    ESP_ERROR_CHECK(esp_a2d_register_callback(a2d_cb));
    ESP_ERROR_CHECK(esp_a2d_sink_init());
    ESP_ERROR_CHECK(esp_a2d_sink_register_data_callback(a2d_data_cb));
    (void)esp_a2d_sink_get_delay_value();
    ESP_ERROR_CHECK(esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE));
    _leds.set_status(dsync::ui::Status::Discoverable);
    ESP_LOGI(TAG, "discoverable as \"%s\"", CONFIG_DSYNC_BT_DEVICE_NAME);
}

void Sink::work_task (void *arg) {
    auto *self = static_cast<Sink *>(arg);
    WorkMsg msg {};

    for (;;) {
        if (xQueueReceive(self->_work_q.get(), &msg, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        switch (msg.event) {
        case WorkEvent::StackUp:
            self->on_stack_up();
            break;
        case WorkEvent::A2dpEvent:
            self->handle_a2d_event(static_cast<uint16_t>(msg.a2d_event), &msg.a2d_param);
            break;
        }
    }
}

esp_err_t Sink::init_controller () {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(err, TAG, "nvs");

    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_BLE));

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_bt_controller_init(&bt_cfg), TAG, "bt_controller_init");
    ESP_RETURN_ON_ERROR(
        esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT),
        TAG,
        "bt_controller_enable");

    esp_bluedroid_config_t bluedroid_cfg = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
#if !CONFIG_DSYNC_BT_SSP_ENABLED
    bluedroid_cfg.ssp_en = false;
#endif
    ESP_RETURN_ON_ERROR(esp_bluedroid_init_with_cfg(&bluedroid_cfg), TAG, "bluedroid_init");
    ESP_RETURN_ON_ERROR(esp_bluedroid_enable(), TAG, "bluedroid_enable");

#if CONFIG_DSYNC_BT_SSP_ENABLED
    esp_bt_sp_param_t param_type = ESP_BT_SP_IOCAP_MODE;
    esp_bt_io_cap_t iocap = ESP_BT_IO_CAP_IO;
    esp_bt_gap_set_security_param(param_type, &iocap, sizeof(uint8_t));
#endif

    esp_bt_pin_type_t pin_type = ESP_BT_PIN_TYPE_FIXED;
    esp_bt_pin_code_t pin_code = {'1', '2', '3', '4'};
    esp_bt_gap_set_pin(pin_type, 4, pin_code);

    char bda_str[18] = {};
    ESP_LOGI(TAG, "BD_ADDR %s", bda2str(esp_bt_dev_get_address(), bda_str, sizeof(bda_str)));
    return ESP_OK;
}

esp_err_t Sink::start () {
    ESP_RETURN_ON_ERROR(init_controller(), TAG, "controller");

    _work_q.reset(xQueueCreate(16, sizeof(WorkMsg)));
    if (!_work_q) {
        return ESP_ERR_NO_MEM;
    }

    TaskHandle_t raw = nullptr;
    if (xTaskCreate(work_task, "bt_work", 4096, this, 5, &raw) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    _work_task.reset(raw);
    _active = this;

    WorkMsg msg {};
    msg.event = WorkEvent::StackUp;
    if (xQueueSend(_work_q.get(), &msg, portMAX_DELAY) != pdTRUE) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

}  // namespace dsync::bt
