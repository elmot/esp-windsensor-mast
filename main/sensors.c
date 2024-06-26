#include "windsensor.h"
#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "esp_log.h"
#include "math.h"

#define WIND_STALL_MS (2000)
#define TIMER_RESOLUTION_HZ (1000000)

#define AVERAGING_BUFFER_SIZE  (20)

static double averaging_buffer_sin[AVERAGING_BUFFER_SIZE];
static double averaging_buffer_cos[AVERAGING_BUFFER_SIZE];
static size_t averaging_idx = 0;

volatile angle_info_t angle_info = {
        .average_time_ms = 2000,
        .angle_corr = 1,
        .too_close_angle = 30,
        .dead_run_angle = 15
};
volatile wind_speed_info_t wind_speed_info = {
        .wind_ticks = -1, .wind_speed_calib = 5, .wind_speed_calib_ticks = 700
};


static volatile bool speed_phase = 0;
static volatile int32_t speed_tick_counter = 0;
static volatile gptimer_handle_t speed_gptimer_handle = 0;

float calcWindSpeed() {
    if (wind_speed_info.wind_ticks < 0) return .0f;
    return (float) wind_speed_info.wind_speed_calib_ticks *
           (float) wind_speed_info.wind_speed_calib / (float) wind_speed_info.wind_ticks;

}

static bool IRAM_ATTR wind_speed_callback(__unused struct gptimer_t *gptimer, __unused const gptimer_alarm_event_data_t *eventData,
                    __unused void *user_data) {
    speed_tick_counter++;
    if (speed_tick_counter < CONFIG_SPEED_SENSOR_DEBOUNCE_MS) return false;
    bool level = gpio_get_level(CONFIG_SPEED_SENSOR_GPIO);
    if ((speed_tick_counter >= WIND_STALL_MS) && (level == speed_phase)) {
        wind_speed_info.wind_ticks = -1;
        //avoid potential overflow
        speed_tick_counter = WIND_STALL_MS + 1;
        return false;
    }
    if (level == 1) {
        speed_phase = 1;
    } else {
        if (speed_phase) {
            wind_speed_info.wind_ticks = speed_tick_counter;

            speed_tick_counter = 0;
        }
        speed_phase = 0;
    }

    return false; // return whether a task switch is needed
}

void initSpeedGpioAndTimers() {
    const gpio_config_t io_conf = {
            //bit mask of the pins that you want to set,e.g.GPIO18/19
            .pin_bit_mask = BIT64(CONFIG_SPEED_SENSOR_GPIO),
            //disable interrupt
            .intr_type = GPIO_INTR_DISABLE,
            //set as output mode
            .mode = GPIO_MODE_INPUT,
            //disable pull-down mode
            .pull_down_en = GPIO_PULLUP_DISABLE,
            //disable pull-up mode
            .pull_up_en = GPIO_PULLDOWN_ENABLE
    };
    gpio_config(&io_conf);

    const gptimer_config_t config = {
            .clk_src = TIMER_SRC_CLK_DEFAULT,
            .direction = GPTIMER_COUNT_UP,
            .resolution_hz = TIMER_RESOLUTION_HZ,
    };
    const gptimer_alarm_config_t alarm_config = {
            .alarm_count = 1000,
            .reload_count = 0,
            .flags = {.auto_reload_on_alarm = 1}
    };
    const gptimer_event_callbacks_t gptimer_callbacks = {
            .on_alarm = wind_speed_callback
    };
    ESP_ERROR_CHECK(gptimer_new_timer(&config, (gptimer_handle_t *) &speed_gptimer_handle));

    // For the timer counter to a initial value
    ESP_ERROR_CHECK(gptimer_set_raw_count(speed_gptimer_handle, 0));
    // Set alarm value and enable alarm interrupt
    ESP_ERROR_CHECK(gptimer_set_alarm_action(speed_gptimer_handle, &alarm_config));
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(speed_gptimer_handle, &gptimer_callbacks, NULL));
    // Start timer
    ESP_ERROR_CHECK(gptimer_enable(speed_gptimer_handle));
    ESP_ERROR_CHECK(gptimer_start(speed_gptimer_handle));
}

_Noreturn void sensor_task(__unused void *args) {
    initAngleSensor();

    TickType_t time = xTaskGetTickCount();
    initSpeedGpioAndTimers();
    while (1) {
        if (readAngle(&angle_info)) {
            int angle_sample = (angle_info.last_raw_angle + angle_info.angle_corr) % 360;
            angle_info.last_angle = angle_sample;
            averaging_buffer_sin[averaging_idx] = sin(angle_sample * M_PI / 360);
            averaging_buffer_cos[averaging_idx] = cos(angle_sample * M_PI / 360);
            averaging_idx = (averaging_idx + 1) % AVERAGING_BUFFER_SIZE;
            double sum_sin = 0;
            double sum_cos = 0;
            for (int i = 0; i < AVERAGING_BUFFER_SIZE; ++i) {
                sum_sin += averaging_buffer_sin[i];
                sum_cos += averaging_buffer_cos[i];
            }
            angle_info.angle = (360 + (int) round(atan2(sum_sin, sum_cos) * 360 / M_PI)) % 360;
            if (angle_info.angle<angle_info.too_close_angle ||
                                 angle_info.angle>(360 - angle_info.too_close_angle)) {
                angle_info.wind_warning = TOO_CLOSE_TO_WIND;
            } else if (angle_info.angle > (180 - angle_info.dead_run_angle) &&
                       angle_info.angle < (180 + angle_info.too_close_angle)) {
                angle_info.wind_warning = DEAD_RUN;
            } else {
                angle_info.wind_warning = NO_WARNING;
            }
        }
        xTaskDelayUntil(&time, configTICK_RATE_HZ * angle_info.average_time_ms / 1000 / AVERAGING_BUFFER_SIZE);
    }
}

const char *sensor_status() {
    switch (angle_info.status) {
        case NO_MAGNET:
            return "NO_MAGNET";
        case FIELD_TOO_LOW:
            return "TOO_LOW";
        case FIELD_TOO_HIGH:
            return "TOO_HIGH";
        case OK:
            return "FINE";
        default:
            return "ERROR";
    }
}

const char *wind_warning_text() {
    switch (angle_info.status) {
        case FIELD_TOO_LOW:
        case FIELD_TOO_HIGH:
        case OK:
            break;
        case NO_MAGNET:
            return "NO_MAGNET";
        default:
            return "ERROR";
    }
    switch (angle_info.wind_warning) {
        case DEAD_RUN:
            return "DEAD_RUN";
        case TOO_CLOSE_TO_WIND:
            return "TOO_CLOSE_TO_WIND";
        default:
            return "NONE";
    }
}

void appendCheckSum(char *nmea_text, size_t len) {
    uint8_t checksum = 0;
    int i = 1;
    for (; (i < len) && nmea_text[i] != 0; i++) {
        checksum = checksum ^ nmea_text[i];
    }
    snprintf(nmea_text + i, len - i, "*%02X\r\n", checksum);
}

_Noreturn void data_broadcast_task(__unused void *args) {
    static char nmea_text[200];
    TickType_t time = xTaskGetTickCount();
    nmea_bcast_init();
    while (1) {
        const char *statusStr = sensor_status();
        float wind = calcWindSpeed();
        if ((angle_info.status == FIELD_TOO_LOW) ||
            (angle_info.status == FIELD_TOO_HIGH) ||
            (angle_info.status == OK)) {
            snprintf(nmea_text, sizeof nmea_text - 5, "$WIMWV,%d.0,R,%.1f,M,A", angle_info.angle, wind);
        } else {
            /*
            MWV Wind Speed and Angle
             1 2 3 4 5
             | | | | |
            $--MWV,x.x,a,x.x,a*hh
            1) Wind Angle, 0 to 360 degrees
            2) Reference, R = Relative, T = True
            3) Wind Speed
            4) Wind Speed Units, K/M/N
            5) Status, A = Data Valid
            6) Checksum
             */
            snprintf(nmea_text, sizeof nmea_text - 5, "$WIMWV,,R,%.1f,M,V", wind);
        }
        appendCheckSum(nmea_text, sizeof nmea_text);

        ESP_LOGD(TAG_SENSOR,
                 "Status: %10s; AGC: x%02x; RAW ANGLE: %3d; ANGLE: %3d; WARN: %s; AVERAGE_ANGLE: %3d; SPEED_TICKS: %ld; SPEED: %f",
                 statusStr,
                 angle_info.agc,
                 angle_info.last_raw_angle,
                 angle_info.last_angle,
                 wind_warning_text(),
                 angle_info.angle,
                 wind_speed_info.wind_ticks,
                 wind);
        ESP_LOGD(TAG_SENSOR, "NMEA: %s", nmea_text);
        nmea_bcast(nmea_text);
        snprintf(nmea_text, sizeof nmea_text - 5, "$PEWWT,%s", wind_warning_text());
        appendCheckSum(nmea_text, sizeof nmea_text);
        ESP_LOGD(TAG_SENSOR, "NMEA: %s", nmea_text);
        nmea_bcast(nmea_text);

        vTaskDelayUntil(&time, configTICK_RATE_HZ / 2);
    }
}

int sensor_response(char *buffer, ssize_t capacity) {
    const char *statusStr = sensor_status();
    return snprintf(buffer, capacity,
                    "{\"sensor\": \"%s\",\"agc\":%d,\"angle\":%d,"
                    "\"speedTicks\": %ld,\"speed\": %3.1f,"
                    "\"warning\": \"%s\","
                    "\"averTime\": %d,\"noSailAngle\": %d,\"deadRunAngle\": %d}",
                    statusStr, angle_info.agc, angle_info.angle,
                    wind_speed_info.wind_ticks, calcWindSpeed(),
                    wind_warning_text(),
                    angle_info.average_time_ms, angle_info.too_close_angle, angle_info.dead_run_angle
    );
}
