#include "ota.h"

#include <sys/param.h>

#include "esp_ota_ops.h"
#include "mbedtls/base64.h"
#include "windsensor.h"

static esp_err_t ota_get_handler(httpd_req_t* req);

static esp_err_t ota_about_get_handler(httpd_req_t* req);

static esp_err_t ota_put_handler(httpd_req_t* req);

void httpd_printf(httpd_req_t* req, const char* format, ...);

bool report_error(httpd_req_t* req, esp_err_t errorCode, const char* fileName, int line);

const httpd_uri_t ota_get = {
    .uri = "/setup/ota",
    .method = HTTP_GET,
    .handler = ota_get_handler
};

const httpd_uri_t ota_about_get = {
    .uri = "/about",
    .method = HTTP_GET,
    .handler = ota_about_get_handler
};

const httpd_uri_t ota_put = {
    .uri = "/setup/ota-put",
    .method = HTTP_PUT,
    .handler = ota_put_handler
};

#define MULTIPART "multipart/form-data"
#define BOUNDARY "boundary="


extern const char ota_start[] asm("_binary_ota_html_start");
extern const char ota_end[] asm("_binary_ota_html_end");
const char* OTA_TAG = "web-ota";
#define OTA_PASSWORD "AlexShilov"

volatile bool ota_busy = false;

bool report_error(httpd_req_t* req, esp_err_t errorCode, const char* fileName, int line)
{
    if (errorCode == ESP_OK) return false;
    httpd_resp_set_type(req, "text/html");
    httpd_printf(req, "<html><head><title>OTA Update Error</title></head>");
    httpd_printf(req, "<body><h1>OTA Update Error</h1>");
    httpd_printf(req, "Code %d(%s) at %s:%d</body></html>",
                 errorCode, esp_err_to_name(errorCode),
                 fileName, line);
    httpd_resp_send_chunk(req, NULL, 0);
    return true;
}

void httpd_printf(httpd_req_t* req, const char* format, ...)
{
    char buffer[400];
    va_list argptr;
    va_start(argptr, format);
    vsnprintf(buffer, 399, format, argptr);
    va_end(argptr);
    httpd_resp_sendstr_chunk(req, buffer);
    ESP_LOGI(OTA_TAG, "HTTP: %s", buffer);
}

static esp_err_t ota_about_get_handler(httpd_req_t* req)
{
    const esp_app_desc_t* desc = esp_app_get_description();
    const esp_partition_t* currentPartition = esp_ota_get_running_partition();
#define STRINGIFY(x) #x
#define INT_TO_STRING(x) STRINGIFY(x)
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Refresh", "3");
    httpd_printf(req, "<html><head><title>About</title></head>"
                 "<body><h1>Yanus wind system</h1>");
    httpd_printf(req, "Name: %s<br>Version: %s<br>", desc->project_name, desc->version);
    httpd_printf(req, "Build timestamp: %s %s<br>IDF version: %s<br>"
                 "Current Partition: %s (0x%08x)<br>",
                 desc->date, desc->time, desc->idf_ver,
                 currentPartition->label, currentPartition->address);
    httpd_printf(req, "<hr>GPIO map:"
                 "<br>I<sub>2</sub>C SCL:" INT_TO_STRING(CONFIG_I2C_MASTER_SCL)
                 "<br>I<sub>2</sub>C SDA:" INT_TO_STRING(CONFIG_I2C_MASTER_SDA)
                 "<br>Speed sensor: " INT_TO_STRING(CONFIG_SPEED_SENSOR_GPIO)
                 "<br>Speed Simulator signal: " INT_TO_STRING(CONFIG_SIMULATION_GPIO)
    );

    httpd_printf(req, "<hr>Angle sensor status: %s; measured angle: %u; speed sensor pulse: %d ms"
                 "<hr><a href=\"/wind\">Wind</a> | "
                 "<a href=\"/setup\">Setup</a> | "
                 "<a href=\"/setup/ota\">Firmware update</a><br>",
                 sensor_status(),
                 angle_info.last_angle,
                 wind_speed_info.wind_ticks);


    httpd_printf(req, "</body></html>");
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t ota_get_handler(httpd_req_t* req)
{
    return page_get_handler(req, ota_start, ota_end);
}

#define BUF_SIZE (16384)
#define BASIC_AUTH_PREFIX "Basic "

static esp_err_t ota_put_handler(httpd_req_t* req)
{
    ESP_LOGI(OTA_TAG, "Upload url: %s", req->uri);
    if (ota_busy)
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Upload process is already underway");
        ESP_LOGE(OTA_TAG, "Upload process is already underway (%d)", 0);
        return ESP_OK;
    }
    ota_busy = true;

    static char buffer[BUF_SIZE + 1];
    buffer[BUF_SIZE] = 0;
    if (httpd_req_get_hdr_value_str(req, "Authorization", buffer,BUF_SIZE) != ESP_OK)
    {
        httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"Yanus Wind System Firmware Update\"");
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, NULL);
        ota_busy = false;
        return ESP_OK;
    }
    bool authOk = strncmp(buffer,BASIC_AUTH_PREFIX, sizeof BASIC_AUTH_PREFIX - 1) == 0;
    if (authOk)
    {
        static unsigned char auth[200];
        size_t auth_len;
        unsigned char* base64Buffer = (unsigned char*)buffer + sizeof BASIC_AUTH_PREFIX - 1;
        authOk = mbedtls_base64_decode(auth, 199, &auth_len,
                                       base64Buffer, strlen((char*)base64Buffer)) == 0;
        authOk &= strcmp((char*)auth, ":" OTA_PASSWORD) == 0;
    }
    if (!authOk)
    {
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, NULL);
        ota_busy = false;
        return ESP_OK;
    }
    uint uploaded_size = 0;
    uint reported_size = 0;
    bool reboot = false;
    const esp_partition_t* updatingPartition = NULL;
    esp_ota_handle_t ota_handle;


    httpd_resp_set_type(req, "text/html");
    for (int leftBytes = req->content_len; leftBytes > 0;)
    {
        const int l = MIN(leftBytes, BUF_SIZE);
        taskYIELD();
        const int readResult = httpd_req_recv(req, buffer, l);
        if (readResult <= 0)
        {
            httpd_resp_send_err(req, 400, "POST error");
            ota_busy = false;
            return ESP_OK;
        }
        if (uploaded_size == 0)
        {
            const esp_partition_t* currentPartition = esp_ota_get_running_partition();
            updatingPartition = esp_ota_get_next_update_partition(currentPartition);
            httpd_printf(req, "Current partition: %s<br>", currentPartition->label);
            httpd_printf(req, "Partition to be upgraded: %s(0x%08x)<br>", updatingPartition->label,
                         (uint)updatingPartition->address);
            esp_err_t result = esp_ota_begin(updatingPartition, OTA_SIZE_UNKNOWN, &ota_handle);
            if (report_error(req, result,__FILE__,__LINE__)) return ESP_OK;
        }
        esp_err_t result = esp_ota_write(ota_handle, buffer, readResult);
        if (report_error(req, result,__FILE__,__LINE__)) return 1;
        uploaded_size += readResult;
        if ((uploaded_size - reported_size) > 100000)
        {
            httpd_printf(req, "Uploaded: %d bytes<br>", uploaded_size);
            reported_size = uploaded_size;
        }

        taskYIELD();
        leftBytes -= readResult;
    }
    httpd_printf(req, "Data stream end, received bytes: %d<hr>", uploaded_size);

    if (report_error(req, esp_ota_end(ota_handle),__FILE__,__LINE__) || updatingPartition == NULL)
    {
        httpd_printf(req, "Upgrade did not happen");
    }
    else
    {
        httpd_printf(req, "Upload seems to be done, processed %d bytes<br>", uploaded_size);
        esp_app_desc_t newDescription;
        esp_ota_get_partition_description(updatingPartition, &newDescription);

        httpd_printf(req, "Updated Partition: %s (0x%08x)<br>", updatingPartition->label, updatingPartition->address);
        httpd_printf(req, "<hr>");
        httpd_printf(req, "Name: %s<br>", newDescription.project_name);
        httpd_printf(req, "Version: %s<br>", newDescription.version);
        httpd_printf(req, "Build timestamp: %s %s<br>", newDescription.date, newDescription.time);
        httpd_printf(req, "IDF version: %s<br>", newDescription.idf_ver);
        httpd_printf(req, "Switching partitions...<br>", newDescription.idf_ver);
        if (report_error(req, esp_ota_set_boot_partition(updatingPartition),__FILE__,__LINE__))
        {
            httpd_printf(req, "Upgrade failed<br>");
        }
        else
        {
            httpd_printf(req, "Upgrade successfull<br>");
        }
        reboot = true;
        httpd_printf(req, "Rebooting...<br>");
    }
    ota_busy = false;
    httpd_resp_send_chunk(req, NULL, 0);
    if (reboot)
    {
        vTaskDelay(500);
        esp_restart();
    }
    return ESP_OK;
}
