#include "bt_sink.hpp"

#include <cstdio>

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

constexpr char TAG[] = "bt_ctrl";

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

}  // namespace

esp_err_t Sink::init_nvs () {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

esp_err_t Sink::enable_controller () {
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_BLE));

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_bt_controller_init(&bt_cfg), TAG, "bt_controller_init");
    ESP_RETURN_ON_ERROR(
        esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT),
        TAG,
        "bt_controller_enable");
    return ESP_OK;
}

esp_err_t Sink::enable_bluedroid () {
    esp_bluedroid_config_t cfg = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
#if !CONFIG_DSYNC_BT_SSP_ENABLED
    cfg.ssp_en = false;
#endif
    ESP_RETURN_ON_ERROR(esp_bluedroid_init_with_cfg(&cfg), TAG, "bluedroid_init");
    ESP_RETURN_ON_ERROR(esp_bluedroid_enable(), TAG, "bluedroid_enable");
    return ESP_OK;
}

void Sink::configure_pairing () {
#if CONFIG_DSYNC_BT_SSP_ENABLED
    esp_bt_sp_param_t param_type = ESP_BT_SP_IOCAP_MODE;
    esp_bt_io_cap_t iocap = ESP_BT_IO_CAP_IO;
    esp_bt_gap_set_security_param(param_type, &iocap, sizeof(uint8_t));
#endif

    esp_bt_pin_type_t pin_type = ESP_BT_PIN_TYPE_FIXED;
    esp_bt_pin_code_t pin_code = {'1', '2', '3', '4'};
    esp_bt_gap_set_pin(pin_type, 4, pin_code);
}

void Sink::log_bd_addr () {
    char bda_str[18] = {};
    ESP_LOGI(TAG, "BD_ADDR %s", bda2str(esp_bt_dev_get_address(), bda_str, sizeof(bda_str)));
}

esp_err_t Sink::init_controller () {
    ESP_RETURN_ON_ERROR(init_nvs(), TAG, "nvs");
    ESP_RETURN_ON_ERROR(enable_controller(), TAG, "controller");
    ESP_RETURN_ON_ERROR(enable_bluedroid(), TAG, "bluedroid");
    configure_pairing();
    log_bd_addr();
    return ESP_OK;
}

}  // namespace dsync::bt
