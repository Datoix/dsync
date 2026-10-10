#include "bt_sink.hpp"

#include <cinttypes>

#include "bt_bda.hpp"
#include "esp_a2dp_api.h"
#include "esp_gap_bt_api.h"
#include "esp_log.h"
#include "sdkconfig.h"

namespace dsync::bt {
namespace {

constexpr char TAG[] = "bt_a2dp";

/** Reported A2DP render delay (1/10 ms units) added to the stack default. */
constexpr uint32_t kAppDelayTenthMs = 50;

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

const char *sbc_ch_mode_str (uint8_t ch_mode) {
    if (ch_mode & ESP_A2D_SBC_CIE_CH_MODE_MONO) {
        return "mono";
    }
    if (ch_mode & ESP_A2D_SBC_CIE_CH_MODE_DUAL_CHANNEL) {
        return "dual";
    }
    if (ch_mode & ESP_A2D_SBC_CIE_CH_MODE_STEREO) {
        return "stereo";
    }
    if (ch_mode & ESP_A2D_SBC_CIE_CH_MODE_JOINT_STEREO) {
        return "joint";
    }
    return "?";
}

void gap_cb (esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param) {
    if (!Sink::active()) {
        return;
    }

    switch (event) {
    case ESP_BT_GAP_AUTH_CMPL_EVT:
        if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
            ESP_LOGI(TAG, "auth ok name=%s", param->auth_cmpl.device_name);
        } else {
            ESP_LOGE(TAG, "auth fail stat=%d", param->auth_cmpl.stat);
        }
        return;
#if CONFIG_DSYNC_BT_SSP_ENABLED
    case ESP_BT_GAP_CFM_REQ_EVT:
        ESP_LOGI(TAG, "ssp confirm=%" PRIu32, param->cfm_req.num_val);
        esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
        return;
    case ESP_BT_GAP_KEY_NOTIF_EVT:
        ESP_LOGI(TAG, "ssp passkey=%" PRIu32, param->key_notif.passkey);
        return;
    case ESP_BT_GAP_KEY_REQ_EVT:
        ESP_LOGW(TAG, "ssp passkey requested");
        return;
#endif
    default:
        return;
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

void Sink::on_connection (void *param) {
    auto *a2d = static_cast<esp_a2d_cb_param_t *>(param);
    static const char *conn_str[] = {"Disconnected", "Connecting", "Connected", "Disconnecting"};

    char bda_str[18] = {};
    ESP_LOGI(
        TAG,
        "a2dp %s peer=%s",
        conn_str[a2d->conn_stat.state],
        bda2str(a2d->conn_stat.remote_bda, bda_str, sizeof(bda_str)));

    if (a2d->conn_stat.state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
        esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
        _audio.stop();  // drop queued audio; the I2S channel is process-lifetime
        _leds.set_status(dsync::ui::Status::Discoverable);
        return;
    }

    if (a2d->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTING) {
        (void)_audio.open();
        return;
    }

    if (a2d->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
        esp_bt_gap_set_scan_mode(ESP_BT_NON_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
        (void)_audio.start();
        _leds.set_status(dsync::ui::Status::Connected);
    }
}

void Sink::on_audio_state (void *param) {
    auto *a2d = static_cast<esp_a2d_cb_param_t *>(param);

    if (a2d->audio_stat.state == ESP_A2D_AUDIO_STATE_STARTED) {
        _audio.reset_stream_stats();
        ESP_LOGI(
            TAG,
            "a2dp audio=Started expect_pcm=%" PRIu32 "B/s (%" PRIu32
            "Hz ch=%d) vol=n/a (no AVRCP)",
            _audio.expect_pcm_bps(),
            _audio.sample_rate_hz(),
            _audio.channel_count());
        _leds.set_status(dsync::ui::Status::Playing);
        return;
    }

    ESP_LOGI(TAG, "a2dp audio=Suspended");
    _leds.set_status(dsync::ui::Status::Connected);
}

void Sink::on_audio_cfg (void *param) {
    auto *a2d = static_cast<esp_a2d_cb_param_t *>(param);
    if (a2d->audio_cfg.mcc.type != ESP_A2D_MCT_SBC) {
        ESP_LOGW(TAG, "codec type=%d (not SBC) ignored", a2d->audio_cfg.mcc.type);
        return;
    }

    const auto &sbc = a2d->audio_cfg.mcc.cie.sbc_info;
    const uint32_t rate = sbc_sample_rate(a2d->audio_cfg.mcc);
    const int ch = sbc_channels(a2d->audio_cfg.mcc);
    const uint32_t expect_pcm = rate * static_cast<uint32_t>(ch) * 2u;

    ESP_LOGI(
        TAG,
        "codec SBC rate=%" PRIu32 "Hz ch=%d (%s) bitpool=%u..%u "
        "block=0x%x subbands=0x%x alloc=0x%x expect_pcm=%" PRIu32 "B/s vol=n/a",
        rate,
        ch,
        sbc_ch_mode_str(sbc.ch_mode),
        sbc.min_bitpool,
        sbc.max_bitpool,
        sbc.block_len,
        sbc.num_subbands,
        sbc.alloc_mthd,
        expect_pcm);

    const esp_err_t cfg_err = _audio.configure_pcm(rate, ch);
    if (cfg_err != ESP_OK) {
        ESP_LOGE(
            TAG,
            "configure_pcm(%" PRIu32 "Hz,%d) failed: %s",
            rate,
            ch,
            esp_err_to_name(cfg_err));
        return;  // keep I2S disabled rather than start with a stale format
    }

    const esp_err_t start_err = _audio.start();
    if (start_err != ESP_OK) {
        ESP_LOGE(TAG, "audio start failed: %s", esp_err_to_name(start_err));
    }
}

void Sink::handle_a2d_event (uint16_t event, void *param) {
    switch (static_cast<esp_a2d_cb_event_t>(event)) {
    case ESP_A2D_CONNECTION_STATE_EVT:
        on_connection(param);
        return;
    case ESP_A2D_AUDIO_STATE_EVT:
        on_audio_state(param);
        return;
    case ESP_A2D_AUDIO_CFG_EVT:
        on_audio_cfg(param);
        return;
    case ESP_A2D_SNK_GET_DELAY_VALUE_EVT: {
        // Reply with the stack default plus our own buffering latency, as the
        // IDF 6.1 reference sink does. Sources that honour delay reporting pace
        // the stream more evenly.
        auto *a2d = static_cast<esp_a2d_cb_param_t *>(param);
        const uint32_t delay = a2d->a2d_get_delay_value_stat.delay_value + kAppDelayTenthMs;
        (void)esp_a2d_sink_set_delay_value(delay);
        ESP_LOGI(TAG, "delay report %" PRIu32 " *0.1ms", delay);
        return;
    }
    case ESP_A2D_PROF_STATE_EVT:
    case ESP_A2D_SNK_PSC_CFG_EVT:
    case ESP_A2D_SNK_SET_DELAY_VALUE_EVT:
        return;
    default:
        return;
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
    ESP_LOGI(
        TAG,
        "ready name=%s avrcp=no vol=n/a",
        CONFIG_DSYNC_BT_DEVICE_NAME);
}

}  // namespace dsync::bt
