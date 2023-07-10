#pragma once

#include "esp_log.h"
#include "esp_http_server.h"

_Noreturn void sensor_task(void *args);

_Noreturn void dev_service_task(void *args);

void registerHttpHandlers(httpd_handle_t server);

typedef enum {
    ERROR, NO_MAGNET, FIELD_TOO_LOW, FIELD_TOO_HIGH, OK
} SENSOR_STATUS;
typedef struct {
    volatile uint16_t average_time_ms;
    volatile uint16_t angle_corr;
    volatile uint16_t last_raw_angle;
    volatile uint16_t last_angle;
    volatile uint16_t angle;
    volatile SENSOR_STATUS status;
} angle_info_t;

extern volatile angle_info_t angle_info;

typedef struct {
    volatile uint16_t wind_speed_calib;
    volatile uint16_t wind_speed_calib_ticks;
    float wind;
} wind_speed_info_t;

extern volatile wind_speed_info_t wind_speed_info;

int sensor_response(char *buffer, ssize_t capacity);

void load_persistent_settings();

void save_persistent_settings();

