#include "ota.h"
#include "multipart_parser.h"

static esp_err_t ota_get_handler(httpd_req_t* req);

static esp_err_t ota_post_handler(httpd_req_t* req);

const httpd_uri_t ota_get = {
    .uri = "/setup/ota",
    .method = HTTP_GET,
    .handler = ota_get_handler
};

const httpd_uri_t ota_post = {
    .uri = "/setup/ota-upload",
    .method = HTTP_POST,
    .handler = ota_post_handler
};

#define MULTIPART "multipart/form-data"
#define BOUNDARY "boundary="

enum FORM_DATA_MACHINE_STATE
{
    SKIP_TO_NEXT_DATA_PART,
    DATA_PART_STARTED,
    CONTENT_DISPOSITION_COMES,
    DATA_COMES,
    ERROR
};

enum  FORM_DATA_MACHINE_STATE postState;
extern const char ota_start[] asm("_binary_ota_html_start");
extern const char ota_end[] asm("_binary_ota_html_end");
const char* OTA_TAG = "web-ota";

static int post_mime_body_end(multipart_parser* parser)
{
    ESP_LOGI(OTA_TAG, "State: %d, body_end", postState);
    return ESP_OK;
}

static int post_mime_part_data_end(multipart_parser* parser)
{
    ESP_LOGI(OTA_TAG, "State: %d, Data End", postState);
    postState = SKIP_TO_NEXT_DATA_PART;
    return ESP_OK;
}

static int post_mime_part_data_begin(multipart_parser* parser)
{
    ESP_LOGI(OTA_TAG, "State: %d, Part Data Begin", postState);
    postState = DATA_PART_STARTED;
    return ESP_OK;
}

static int post_mime_header_field(multipart_parser*, const char* at, size_t length)
{
    ESP_LOGI(OTA_TAG, "State:%d, Header Field: %.*s", postState, length, at);
    if (postState == DATA_PART_STARTED)
    {
        if (strncasecmp("Content-Disposition", at, length) == 0)
        {
            postState = CONTENT_DISPOSITION_COMES;
        }
    }
    return ESP_OK;
}

static int post_mime_header_value(multipart_parser*, const char* at, size_t length)
{
    ESP_LOGI(OTA_TAG, "State: %d, Header Value: %.*s", postState, length, at);
    if (postState == CONTENT_DISPOSITION_COMES)
    {
        if (strnstr(at, "name=\"ota_data\"", length) == NULL)
        {
            ESP_LOGI(OTA_TAG, "Wrong content-disposition");
            postState = SKIP_TO_NEXT_DATA_PART;
        } else
        {
            ESP_LOGI(OTA_TAG, "Data chunks are expected");
            postState = DATA_COMES;
        }
    }
    return ESP_OK;
}

static int post_mime_data(multipart_parser*, const char* at, size_t length)
{
    ESP_LOGI(OTA_TAG, "Data chunk: %i bytes", length);
    return ESP_OK;
}

const multipart_parser_settings callbacks = {
    .on_body_end = post_mime_body_end,
    .on_headers_complete = NULL,
    .on_header_field = post_mime_header_field,
    .on_header_value = post_mime_header_value,
    .on_part_data_begin = post_mime_part_data_begin,
    .on_part_data_end = post_mime_part_data_end,
    .on_part_data = post_mime_data
};

static esp_err_t ota_get_handler(httpd_req_t* req)
{
    return page_get_handler(req, ota_start, ota_end);
}

#define BUF_SIZE (1024)

static char* verifyHttpFindBoundary(httpd_req_t* req, char* buffer)
{
    const int readResult = httpd_req_get_hdr_value_str(req, "content-type", buffer, BUFSIZ);
    if (readResult != ESP_OK)
    {
        ESP_LOGI(OTA_TAG, "content-type error %d", readResult);
        httpd_resp_send_err(req, 400, "POST Content-Type error");
        return NULL;
    }

    ESP_LOGI(OTA_TAG, "Content-type: %s", buffer);
    if (strncasecmp(MULTIPART, buffer, sizeof(MULTIPART) - 1) != 0)
    {
        ESP_LOGI(OTA_TAG, "Unexpected content-type");
        httpd_resp_send_err(req, 400, "POST Unexpected Content-Type");
        return NULL;
    }
    char* boundaryPtr = strcasestr(buffer,BOUNDARY);
    if (boundaryPtr == NULL)
    {
        ESP_LOGI(OTA_TAG, "Content-type Boundary Error");
        httpd_resp_send_err(req, 400, "Content-type Boundary Error");
        return NULL;
    }
    boundaryPtr += sizeof(BOUNDARY) - 1;
    ESP_LOGI(OTA_TAG, "Boundary: %s", boundaryPtr);
    return boundaryPtr;
}

static esp_err_t ota_post_handler(httpd_req_t* req)
{
    ESP_LOGI(OTA_TAG, "Upload url: %s", req->uri);
    static char buffer[BUF_SIZE + 1];
    buffer[BUF_SIZE] = 0;
    const char* boundaryPtr = verifyHttpFindBoundary(req, buffer);
    if (boundaryPtr == NULL) return ESP_OK;

    multipart_parser* parser = multipart_parser_init(boundaryPtr, &callbacks);
    for (int leftBytes = req->content_len; leftBytes > 0;)
    {
        const int l = MIN(leftBytes, BUF_SIZE);
        const int readResult = httpd_req_recv(req, buffer, l);
        if (readResult <= 0)
        {
            httpd_resp_send_err(req, 400, "POST error");
            return ESP_OK;
        }
        const int parsed = multipart_parser_execute(parser, buffer, readResult);
        if (readResult != parsed)
        {
            httpd_resp_send_err(req, 400, "Multipart decode error");
            return ESP_OK;
        }
        leftBytes -= readResult;
    }
    multipart_parser_free(parser);
    const char* const start = ota_start;
    const char* const end = ota_end;
    const ssize_t root_len = end - start;
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, start, root_len);
    return ESP_OK;
}
