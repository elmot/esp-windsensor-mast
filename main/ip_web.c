#include "windsensor.h"
#include "lwip/sockets.h"

const char* WEB_TAG = "web";
extern const char root_start[] asm("_binary_root_html_start");
extern const char root_end[] asm("_binary_root_html_end");

extern const char setup_start[] asm("_binary_setup_html_start");
extern const char setup_end[] asm("_binary_setup_html_end");

// HTTP GET Handlers
esp_err_t page_get_handler(httpd_req_t* req, const char* const start, const char* const end)
{
    const ssize_t root_len = end - start;
    ESP_LOGI(WEB_TAG, "Serve url: %s", req->uri);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, start, root_len);
    return ESP_OK;
}

// ReSharper disable once CppDFAConstantFunctionResult
static esp_err_t wind_get_handler(httpd_req_t* req)
{
    return page_get_handler(req, root_start, root_end);
}

// ReSharper disable once CppDFAConstantFunctionResult
static esp_err_t setup_get_handler(httpd_req_t* req)
{
    return page_get_handler(req, setup_start, setup_end);
}

static esp_err_t root_get_handler(httpd_req_t* req)
{
    httpd_resp_set_status(req, "302 Temporary Redirect");
    httpd_resp_set_hdr(req, "Location", "http://yanus.local/wind");
    // iOS requires content in the response to detect a captive portal, simply redirecting is not sufficient.
    httpd_resp_send(req, "Redirect to the captive portal", HTTPD_RESP_USE_STRLEN);

    ESP_LOGI(WEB_TAG, "Redirecting to wind: %s", req->uri);
    return ESP_OK;
}

static const httpd_uri_t root = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = root_get_handler
};

static const httpd_uri_t setup = {
    .uri = "/setup",
    .method = HTTP_GET,
    .handler = setup_get_handler
};

static const httpd_uri_t wind = {
    .uri = "/wind",
    .method = HTTP_GET,
    .handler = wind_get_handler
};

static esp_err_t data_handler(httpd_req_t* req)
{
    char data_response[500];
    int data_len = sensor_response(data_response, sizeof(data_response));
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, data_response, data_len);
    return ESP_OK;
}

static const httpd_uri_t data = {
    .uri = "/data",
    .method = HTTP_GET,
    .handler = data_handler
};

// HTTP Error (404) Handler - Redirects all requests to the root page
// ReSharper disable once CppDFAConstantFunctionResult
esp_err_t http_404_error_handler(httpd_req_t* req, __unused httpd_err_code_t err)
{
    return root_get_handler(req);
}

void registerHttpHandlers(httpd_handle_t server)
{
    httpd_register_uri_handler(server, &root);
    httpd_register_uri_handler(server, &wind);
    httpd_register_uri_handler(server, &data);
    httpd_register_uri_handler(server, &setup);
    httpd_register_uri_handler(server, &ota_get);
    httpd_register_uri_handler(server, &ota_about_get);
    httpd_register_uri_handler(server, &ota_put);
    httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, http_404_error_handler);
}

volatile static int sock;
void nmea_bcast_init()
{
    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0)
    {
        ESP_LOGE(WEB_TAG, "Unable to create socket: errno %d", errno);
    }
    else
    {
        ESP_LOGI(WEB_TAG, "Socket created, sending to port %d", CONFIG_NMEA_UDP_PORT);
    }
}

void nmea_bcast(const char* text)
{
#ifdef __CLION_IDE__
#define __builtin_bswap16(a) a
#endif
    const struct sockaddr_in dest_addr = {
        .sin_family = AF_INET,
        .sin_addr = {
            .s_addr = IPADDR_BROADCAST
        },
        .sin_port = htons(CONFIG_NMEA_UDP_PORT)
    };

    const size_t sendlen = sendto(sock,text,strlen(text),0,(struct sockaddr *)&dest_addr,sizeof (dest_addr));
    if (sendlen <= 0) {
        ESP_LOGD(WEB_TAG, "send failed, sendto returned  %d", sendlen);
    }
}
