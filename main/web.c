#include "windsensor.h"

static const char *TAG = "web";
extern const char root_start[] asm("_binary_root_html_start");
extern const char root_end[] asm("_binary_root_html_end");
extern const char setup_start[] asm("_binary_setup_html_start");
extern const char setup_end[] asm("_binary_setup_html_end");

// HTTP GET Handlers
static esp_err_t page_get_handler(httpd_req_t *req) {
    const ssize_t root_len = root_end - root_start;
    ESP_LOGI(TAG, "Serve url: %s", req->uri);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, root_start, root_len);

    return ESP_OK;
}

static esp_err_t root_get_handler(httpd_req_t *req) {
    httpd_resp_set_status(req, "302 Temporary Redirect");
    httpd_resp_set_hdr(req, "Location", "http://yanus.wind/wind");
    // iOS requires content in the response to detect a captive portal, simply redirecting is not sufficient.
    httpd_resp_send(req, "Redirect to the captive portal", HTTPD_RESP_USE_STRLEN);

    ESP_LOGI(TAG, "Redirecting to wind: %s", req->uri);
    return ESP_OK;

}

static const httpd_uri_t root = {
        .uri = "/",
        .method = HTTP_GET,
        .user_ctx= (char *[2]) {root_start, root_end},
        .handler = page_get_handler
};

/*
static const httpd_uri_t setup = {
        .uri = "/setup",
        .method = HTTP_GET,
        .user_ctx= {ser, root_end},
        .handler = page_handler
};
*/

static const httpd_uri_t wind = {
        .uri = "/wind",
        .method = HTTP_GET,
        .handler = page_get_handler
};

static esp_err_t data_handler(httpd_req_t *req) {
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
esp_err_t http_404_error_handler(httpd_req_t *req, __unused httpd_err_code_t err) {
    return root_get_handler(req);
}

void registerHttpHandlers(httpd_handle_t server) {
    httpd_register_uri_handler(server, &root);
    httpd_register_uri_handler(server, &wind);
    httpd_register_uri_handler(server, &data);
    httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, http_404_error_handler);
}
