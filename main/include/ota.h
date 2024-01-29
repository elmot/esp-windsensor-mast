//
// Created by elmot on 17 Jan 2024.
//

#ifndef OTA_H
#define OTA_H
#include "esp_log.h"
#include "esp_http_server.h"

extern const httpd_uri_t ota_post;
extern const httpd_uri_t ota_get;
extern const char *WEB_TAG;
esp_err_t page_get_handler(httpd_req_t *req, const char * start, const char * end);

#endif //OTA_H
