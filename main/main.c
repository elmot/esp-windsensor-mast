/* Captive Portal Example

    This example code is in the Public Domain (or CC0 licensed, at your option.)

    Unless required by applicable law or agreed to in writing, this
    software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
    CONDITIONS OF ANY KIND, either express or implied.
*/

#include <sys/param.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"

#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "lwip/inet.h"

#include "dns_server.h"

#include "windsensor.h"
#include "freertos/task.h"

static void wifi_event_handler(__unused void *arg, __unused esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *) event_data;
        ESP_LOGI(TAG_WEB, "station " MACSTR " join, AID=%d",
                 MAC2STR(event->mac), event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *) event_data;
        ESP_LOGI(TAG_WEB, "station " MACSTR " leave, AID=%d",
                 MAC2STR(event->mac), event->aid);
    }
}

static void wifi_init_softap(void) {
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));

    wifi_config_t wifi_config = {
            .ap = {
                    .ssid = CONFIG_ESP_WIFI_SSID,
                    .ssid_len = strlen(CONFIG_ESP_WIFI_SSID),
                    .password = CONFIG_ESP_WIFI_PASSWORD,
                    .max_connection = CONFIG_ESP_MAX_STA_CONN,
                    .authmode = WIFI_AUTH_WPA_WPA2_PSK
            },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    esp_netif_ip_info_t ip_info;
    esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_AP_DEF"), &ip_info);

    char ip_addr[16];
    inet_ntoa_r(ip_info.ip.addr, ip_addr, 16);
    ESP_LOGI(TAG_WEB, "Set up softAP with IP: %s", ip_addr);

    ESP_LOGI(TAG_WEB, "wifi_init_softap finished. SSID:'%s' password:'%s'",
             CONFIG_ESP_WIFI_SSID, CONFIG_ESP_WIFI_PASSWORD);
}


static httpd_handle_t start_webserver(void) {
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets = 10;
    config.lru_purge_enable = true;

    // Start the httpd server
    ESP_LOGI(TAG_WEB, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        // Set URI handlers
        ESP_LOGI(TAG_WEB, "Registering URI handlers");
        registerHttpHandlers(server);
    }
    return server;
}


TaskHandle_t sensor_task_handle;
TaskHandle_t dev_service_task_handle;
TaskHandle_t wind_freq_generator_task_handle;

void logNvsParams();

#define NVS_NS "settings"
#define NVS_KEY_ANGLE_CORRECTION "angle_corr"
#define NVS_KEY_ANGLE_AVERAGE_MS "angle_aver_ms"
#define NVS_KEY_ANGLE_DEAD_RUN "angle_dead"
#define NVS_KEY_ANGLE_TOO_CLOSE "angle_too_close"
#define NVS_KEY_WIND_SPEED "wnd_spd"
#define NVS_KEY_WIND_SPEED_TICKS "wnd_spd_ticks"

void load_persistent_settings() {
    nvs_handle_t nvs_handle;

    esp_err_t err = ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_flash_init());
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS partition was truncated and needs to be erased
        // Retry nvs_flash_init
        ESP_LOGI(TAG_SYS, "Outdated NVS partition - %s(%d), erasing", esp_err_to_name(err), err);
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }
    esp_err_t status = ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_open(NVS_NS, NVS_READONLY, &nvs_handle));
    if (status == ESP_ERR_NVS_NOT_INITIALIZED) {
        ESP_LOGI(TAG_SYS, "NVS is empty; using default values");
        return;
    }
    ESP_ERROR_CHECK_WITHOUT_ABORT(
            nvs_get_u16(nvs_handle, NVS_KEY_ANGLE_CORRECTION, (uint16_t *) &angle_info.angle_corr));
    ESP_ERROR_CHECK_WITHOUT_ABORT(
            nvs_get_u16(nvs_handle, NVS_KEY_ANGLE_DEAD_RUN, (uint16_t *) &angle_info.dead_run_angle));
    ESP_ERROR_CHECK_WITHOUT_ABORT(
            nvs_get_u16(nvs_handle, NVS_KEY_ANGLE_TOO_CLOSE, (uint16_t *) &angle_info.too_close_angle));
    ESP_ERROR_CHECK_WITHOUT_ABORT(
            nvs_get_u16(nvs_handle, NVS_KEY_ANGLE_AVERAGE_MS, (uint16_t *) &angle_info.average_time_ms));
    if(angle_info.average_time_ms < 500 || angle_info.average_time_ms > 10000) {
        ESP_LOGI(TAG_SYS, "Loaded averaging time is wrong. Defaulting to 2000");
        angle_info.average_time_ms =2000;
    }
    ESP_ERROR_CHECK_WITHOUT_ABORT(
            nvs_get_u16(nvs_handle, NVS_KEY_WIND_SPEED, (uint16_t *) &wind_speed_info.wind_speed_calib));
    ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_get_u32(nvs_handle,
                                              NVS_KEY_WIND_SPEED_TICKS,
                                              (uint32_t *) &wind_speed_info.wind_speed_calib_ticks));
    nvs_close(nvs_handle);
    logNvsParams();
}

void logNvsParams() {
    ESP_LOGI(TAG_SYS, "NVS parameters: "
                      " angle correction %d;"
                      " dead run angle %d;"
                      " too close angle %d; "
                      " averaging time %d;"
                      " wind speed calibrated %d;"
                      " wind speed ticks %ld",

             angle_info.angle_corr,
             angle_info.dead_run_angle,
             angle_info.too_close_angle,
             angle_info.average_time_ms,
             wind_speed_info.wind_speed_calib,
             wind_speed_info.wind_speed_calib_ticks);
}

void save_persistent_settings() {
    nvs_handle_t nvs_handle;
    ESP_LOGI(TAG_SYS, "Saving persistent values. Opening NVS.");
    esp_err_t openErr =
            ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_open(NVS_NS, NVS_READWRITE, &nvs_handle));
    switch (openErr) {
        case ESP_OK:
            ESP_LOGI(TAG_SYS, "NVS open");
            break;
        case ESP_ERR_NVS_NOT_INITIALIZED:
        case ESP_ERR_NVS_READ_ONLY:
        case ESP_ERR_NVS_NOT_ENOUGH_SPACE:
        case ESP_ERR_NVS_INVALID_STATE:
        case ESP_ERR_NVS_INVALID_LENGTH:
        case ESP_ERR_NVS_NO_FREE_PAGES:
            ESP_LOGI(TAG_SYS, "NVS Init");
            ESP_ERROR_CHECK(nvs_flash_init());
            ESP_LOGI(TAG_SYS, "NVS reopen");
            ESP_ERROR_CHECK(nvs_open(NVS_NS, NVS_READWRITE, &nvs_handle));
            break;
        default:
            ESP_LOGE(TAG_SYS, "NVS open failed");
            abort();
    }
    ESP_LOGI(TAG_SYS, "Saving...");
    logNvsParams();
    ESP_ERROR_CHECK(nvs_set_u16(nvs_handle, NVS_KEY_ANGLE_CORRECTION, angle_info.angle_corr));
    ESP_ERROR_CHECK(nvs_set_u16(nvs_handle, NVS_KEY_ANGLE_DEAD_RUN, angle_info.dead_run_angle));
    ESP_ERROR_CHECK(nvs_set_u16(nvs_handle, NVS_KEY_ANGLE_TOO_CLOSE, angle_info.too_close_angle));
    ESP_ERROR_CHECK(nvs_set_u16(nvs_handle, NVS_KEY_ANGLE_AVERAGE_MS, angle_info.average_time_ms));
    ESP_ERROR_CHECK(nvs_set_u16(nvs_handle, NVS_KEY_WIND_SPEED, wind_speed_info.wind_speed_calib));
    ESP_ERROR_CHECK(nvs_set_u32(nvs_handle, NVS_KEY_WIND_SPEED_TICKS, wind_speed_info.wind_speed_calib_ticks));
    ESP_ERROR_CHECK(nvs_commit(nvs_handle));
    nvs_close(nvs_handle);
}

__unused void app_main(void) {
    /*
        Turn of warnings from HTTP server as redirecting traffic will yield
        lots of invalid requests
    */
    esp_log_level_set("httpd_uri", ESP_LOG_ERROR);
    esp_log_level_set("httpd_txrx", ESP_LOG_ERROR);
    esp_log_level_set("httpd_parse", ESP_LOG_ERROR);

    load_persistent_settings();

    // Initialize networking stack
    ESP_ERROR_CHECK(esp_netif_init());

    // Create default event loop needed by the  main app
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Initialize NVS needed by Wi-Fi
    ESP_ERROR_CHECK(nvs_flash_init());

    // Initialize Wi-Fi including netif with default config
    esp_netif_create_default_wifi_ap();

    // Initialise ESP32 in SoftAP mode
    wifi_init_softap();

    // Start the server for the first time
    start_webserver();

    // Start the DNS server that will redirect all queries to the softAP IP
    start_dns_server();

    xTaskCreate(sensor_task, "Sensor Task", 4096, NULL, 10, &sensor_task_handle);
    xTaskCreate(data_broadcast_task, "Data broadcast", 4096, NULL, 10, &dev_service_task_handle);

    xTaskCreate(wind_freq_generator, "Simulate wind", 4096, NULL, 10, &wind_freq_generator_task_handle);
}
