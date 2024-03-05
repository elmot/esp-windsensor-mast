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

static const char* TAG = "windsensor";

static void wifi_event_handler(__unused void* arg, __unused esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED)
    {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*)event_data;
        ESP_LOGI(TAG, "station " MACSTR " join, AID=%d",
                 MAC2STR(event->mac), event->aid);
    }
    else if (event_id == WIFI_EVENT_AP_STADISCONNECTED)
    {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*)event_data;
        ESP_LOGI(TAG, "station " MACSTR " leave, AID=%d",
                 MAC2STR(event->mac), event->aid);
    }
}

static void wifi_init_softap(void)
{
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
    ESP_LOGI(TAG, "Set up softAP with IP: %s", ip_addr);

    ESP_LOGI(TAG, "wifi_init_softap finished. SSID:'%s' password:'%s'",
             CONFIG_ESP_WIFI_SSID, CONFIG_ESP_WIFI_PASSWORD);
}


static httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets = 13;
    config.lru_purge_enable = true;

    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK)
    {
        // Set URI handlers
        ESP_LOGI(TAG, "Registering URI handlers");
        registerHttpHandlers(server);
    }
    return server;
}


TaskHandle_t sensor_task_handle;
TaskHandle_t dev_service_task_handle;
TaskHandle_t wind_freq_generator_task_handle;

#define NVS_NS "settings"
#define NVS_KEY_ANGLE_CORRECTION "angle_corr"
#define NVS_KEY_ANGLE_AVERAGE_MS "angle_average_ms"
#define NVS_KEY_WIND_SPEED "wind_speed"
#define NVS_KEY_WIND_SPEED_TICKS "wind_speed_ticks"

void load_persistent_settings()
{
    nvs_handle_t nvs_handle;
    esp_err_t status = nvs_open(NVS_NS, NVS_READONLY, &nvs_handle);
    if (status == ESP_ERR_NVS_NOT_FOUND) return;
    ESP_ERROR_CHECK_WITHOUT_ABORT(status);
    ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_get_u16(nvs_handle, NVS_KEY_ANGLE_CORRECTION, (uint16_t*)&angle_info.angle_corr));
    ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_get_u16(nvs_handle, NVS_KEY_ANGLE_CORRECTION, (uint16_t*)&angle_info.dead_run_angle_warning));
    ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_get_u16(nvs_handle, NVS_KEY_ANGLE_CORRECTION, (uint16_t*)&angle_info.too_close_angle_warning));
    ESP_ERROR_CHECK_WITHOUT_ABORT(
        nvs_get_u16(nvs_handle, NVS_KEY_ANGLE_AVERAGE_MS, (uint16_t*)&angle_info.average_time_ms));
    ESP_ERROR_CHECK_WITHOUT_ABORT(
        nvs_get_u16(nvs_handle, NVS_KEY_WIND_SPEED, (uint16_t*)&wind_speed_info.wind_speed_calib));
    ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_get_u16(nvs_handle,
        NVS_KEY_WIND_SPEED_TICKS, (uint16_t*)&wind_speed_info.wind_speed_calib_ticks));
    nvs_close(nvs_handle);
}

void save_persistent_settings()
{
    nvs_handle_t nvs_handle;
    ESP_ERROR_CHECK(nvs_open(NVS_NS, NVS_READWRITE, &nvs_handle));
    ESP_ERROR_CHECK(nvs_set_i16(nvs_handle, NVS_KEY_ANGLE_CORRECTION, angle_info.angle_corr));
    ESP_ERROR_CHECK(nvs_set_i16(nvs_handle, NVS_KEY_ANGLE_CORRECTION, angle_info.dead_run_angle_warning));
    ESP_ERROR_CHECK(nvs_set_i16(nvs_handle, NVS_KEY_ANGLE_CORRECTION, angle_info.too_close_angle_warning));
    ESP_ERROR_CHECK(nvs_set_i16(nvs_handle, NVS_KEY_ANGLE_AVERAGE_MS, angle_info.average_time_ms));
    ESP_ERROR_CHECK(nvs_set_i16(nvs_handle, NVS_KEY_WIND_SPEED, wind_speed_info.wind_speed_calib));
    ESP_ERROR_CHECK(nvs_set_i16(nvs_handle, NVS_KEY_WIND_SPEED_TICKS, wind_speed_info.wind_speed_calib_ticks));
    nvs_close(nvs_handle);
}

void app_main(void)
{
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
