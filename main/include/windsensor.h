#pragma once

#include "esp_log.h"
#include "esp_http_server.h"

_Noreturn void sensor_task(void* args);
_Noreturn void dev_service_task(void* args);
void registerHttpHandlers(httpd_handle_t server);

typedef enum  {ERROR, NO_MAGNET, FIELD_TOO_LOW,FIELD_TOO_HIGH, OK} SENSOR_STATUS;
typedef struct {
    uint16_t average_time_seconds;
    uint16_t angle_shift;
    uint16_t last_raw_angle;
    uint16_t last_angle;
    uint16_t angle;
    SENSOR_STATUS status;
} angle_info_t;
extern volatile angle_info_t angle_info;

int sensor_response(char *buffer, ssize_t capacity);

