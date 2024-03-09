#pragma once

#include "esp_log.h"
#include "esp_http_server.h"
#include "ota.h"


#define TAG_SENSOR  "wind-sensor"
#define TAG_WEB  "wind-comm"
#define TAG_SYS  "wind-sys"

enum SENSOR_STATUS
{
    ERROR, NO_MAGNET, FIELD_TOO_LOW, FIELD_TOO_HIGH, OK
};

enum WIND_WARNING
{
    NO_WARNING, TOO_CLOSE_TO_WIND, DEAD_RUN
};

typedef struct
{
    volatile uint16_t average_time_ms;
    volatile uint16_t angle_corr;
    volatile uint16_t dead_run_angle;
    volatile uint16_t too_close_angle;
    volatile uint16_t last_raw_angle;
    volatile uint16_t last_angle;
    volatile uint16_t angle;
    volatile enum SENSOR_STATUS status;
    volatile enum WIND_WARNING wind_warning;
    volatile uint16_t agc;
} angle_info_t;

extern volatile angle_info_t angle_info;

typedef struct
{
    volatile uint16_t wind_speed_calib;
    volatile uint32_t wind_speed_calib_ticks;
    volatile uint32_t wind_ticks;
    float wind;
} wind_speed_info_t;

extern volatile wind_speed_info_t wind_speed_info;

int sensor_response(char* buffer, ssize_t capacity);

void load_persistent_settings();

void save_persistent_settings();

const char* sensor_status();

void initAngleSensor();

bool readAngle(volatile angle_info_t* angle_info);

void nmea_bcast_init();

void nmea_bcast(const char* text);

_Noreturn void sensor_task(void* args);

_Noreturn void data_broadcast_task(void* args);

_Noreturn void wind_freq_generator(void* args);

void registerHttpHandlers(httpd_handle_t server);

bool verifySetupAuthorization( httpd_req_t *req);
