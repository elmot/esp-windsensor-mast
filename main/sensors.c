#include "windsensor.h"
#include "driver/i2c.h"
#include "driver/gptimer.h"
#include "esp_log.h"
#include "math.h"

#define SPEED_SENSOR_GPIO GPIO_NUM_9
#define SPEED_SENSOR_DEBOUNCE_MS (30)
#define TIMER_RESOLUTION_HZ (1000000)
#define WIND_STALL_MS (2000)

#define I2C_MASTER_RX_BUF_DISABLE 0
#define I2C_MASTER_TX_BUF_DISABLE 0

#define I2C_MASTER_PORT 0
#define  AS560x_ADDR 0x36

#define AS5600_OUT_REG (0x0B)
#define AS5600_OUT_REG_SIZE (0x1C - 0x0B + 1)
#define AS5600_REG_STATUS (0x00)
#define AS5600_REG_RAW_ANGLE_H (0x0C - AS5600_OUT_REG)
#define AS5600_REG_RAW_ANGLE_L (0x0D - AS5600_OUT_REG)
#define AS5600_REG_ANGLE_H (0x0E - AS5600_OUT_REG)
#define AS5600_REG_ANGLE_L (0x0F - AS5600_OUT_REG)
#define AS5600_REG_AGC (0x1A - AS5600_OUT_REG)
#define AS5600_REG_RAW_MAGNITUDE_H (0x1B - AS5600_OUT_REG)
#define AS5600_REG_RAW_MAGNITUDE_L (0x1C - AS5600_OUT_REG)
#define AS5600_STATUS_MH (0x08)
#define AS5600_STATUS_ML (0x10)
#define AS5600_STATUS_MD (0x20)

#define AVERAGING_BUFFER_SIZE  (20)
static const char* TAG_ANGLE = "mech-wind";

static double averaging_buffer_sin[AVERAGING_BUFFER_SIZE];
static double averaging_buffer_cos[AVERAGING_BUFFER_SIZE];
static size_t averaging_idx = 0;

volatile angle_info_t angle_info = {.average_time_ms = 2000, .angle_corr = 1};
volatile wind_speed_info_t wind_speed_info = {
    .wind = 3, .wind_ticks = -1, .wind_speed_calib = 5, .wind_speed_calib_ticks = 700
};


static volatile uint16_t agc;

static volatile bool speed_phase = 0;
static volatile uint32_t speed_tick_counter = 0;
static volatile gptimer_handle_t speed_gptimer_handle = 0;


// ReSharper disable once CppDFAConstantFunctionResult
static bool IRAM_ATTR wind_speed_callback(struct gptimer_t*, const gptimer_alarm_event_data_t*, void*)
{
    speed_tick_counter++;
    if (speed_tick_counter < SPEED_SENSOR_DEBOUNCE_MS) return false;
    if (speed_tick_counter >= WIND_STALL_MS)
    {
        wind_speed_info.wind_ticks = -1;
        wind_speed_info.wind = 0.0f;
        //avoid potential overflow
        speed_tick_counter = WIND_STALL_MS + 1;
        return false;
    }
    bool level = gpio_get_level(SPEED_SENSOR_GPIO);
    if (level == 1)
    {
        speed_phase = 1;
    }
    else
    {
        if (speed_phase)
        {
            wind_speed_info.wind_ticks = speed_tick_counter;
            wind_speed_info.wind = (float)wind_speed_info.wind_speed_calib_ticks *
                wind_speed_info.wind_speed_calib / speed_tick_counter;

            speed_tick_counter = 0;
        }
        speed_phase = 0;
    }

    return false; // return whether a task switch is needed
}

void initSpeedGpioAndTimers()
{
    const gpio_config_t io_conf = {
        //bit mask of the pins that you want to set,e.g.GPIO18/19
        .pin_bit_mask = BIT64(GPIO_NUM_9),
        //disable interrupt
        .intr_type = GPIO_INTR_DISABLE,
        //set as output mode
        .mode = GPIO_MODE_INPUT,
        //disable pull-down mode
        .pull_down_en = GPIO_PULLUP_ENABLE,
        //disable pull-up mode
        .pull_up_en = GPIO_PULLDOWN_DISABLE
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
    ESP_ERROR_CHECK(gptimer_new_timer( &config, (gptimer_handle_t*)&speed_gptimer_handle));

    // For the timer counter to a initial value
    ESP_ERROR_CHECK(gptimer_set_raw_count(speed_gptimer_handle, 0));
    // Set alarm value and enable alarm interrupt
    ESP_ERROR_CHECK(gptimer_set_alarm_action(speed_gptimer_handle,&alarm_config));
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(speed_gptimer_handle,&gptimer_callbacks,NULL));
    // Start timer
    ESP_ERROR_CHECK(gptimer_enable(speed_gptimer_handle));
    ESP_ERROR_CHECK(gptimer_start(speed_gptimer_handle));
}

_Noreturn void sensor_task(__unused void* args)
{
    ESP_LOGI(TAG_ANGLE, "Initialize i2c");
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = CONFIG_I2C_MASTER_SDA, // select SDA GPIO specific to your project
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = CONFIG_I2C_MASTER_SCL, // select SCL GPIO specific to your project
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = CONFIG_I2C_MASTER_FREQ_HZ, // select frequency specific to your project
        .clk_flags = 0, // optional; you can use I2C_SCLK_SRC_FLAG_* flags to choose i2c source clock here
    };


    ESP_ERROR_CHECK(i2c_param_config(I2C_MASTER_PORT, &conf));

    ESP_ERROR_CHECK(
        i2c_driver_install(I2C_MASTER_PORT, conf.mode, I2C_MASTER_RX_BUF_DISABLE, I2C_MASTER_TX_BUF_DISABLE, 0));
    ESP_LOGI(TAG_ANGLE, "i2c initialization done");
    TickType_t time = xTaskGetTickCount();
    initSpeedGpioAndTimers();
    while (1)
    {
        uint8_t buff[AS5600_OUT_REG_SIZE] = {AS5600_OUT_REG}; //Start reading from status, 18 bytes
        esp_err_t sensor_status = i2c_master_write_read_device(I2C_MASTER_PORT, AS560x_ADDR, buff, 1, buff,
                                                               sizeof(buff), 100);
        if (sensor_status != ESP_OK)
        {
            angle_info.status = ERROR;
        }
        else
        {
            agc = buff[AS5600_REG_AGC];
            uint8_t status = buff[AS5600_REG_STATUS];
            if (status & AS5600_STATUS_MD)
            {
                if (status & AS5600_STATUS_ML)
                {
                    angle_info.status = FIELD_TOO_LOW;
                }
                else if (status & AS5600_STATUS_MH)
                {
                    angle_info.status = FIELD_TOO_HIGH;
                }
                else
                {
                    angle_info.status = OK;
                }
            }
            else
            {
                angle_info.status = NO_MAGNET;
            }
            angle_info.last_raw_angle = (buff[AS5600_REG_ANGLE_H] * 256 + buff[AS5600_REG_ANGLE_L]) * 360 / 4096;
            int angle_sample = (angle_info.last_raw_angle + angle_info.angle_corr) % 360;
            angle_info.last_angle = angle_sample;
            averaging_buffer_sin[averaging_idx] = sin(angle_sample * M_PI / 360);
            averaging_buffer_cos[averaging_idx] = cos(angle_sample * M_PI / 360);
            averaging_idx = (averaging_idx + 1) % AVERAGING_BUFFER_SIZE;
            double sum_sin = 0;
            double sum_cos = 0;
            for (int i = 0; i < AVERAGING_BUFFER_SIZE; ++i)
            {
                sum_sin += averaging_buffer_sin[i];
                sum_cos += averaging_buffer_cos[i];
            }
            angle_info.angle = (360 + (int)round(atan2(sum_sin, sum_cos) * 360 / M_PI)) % 360;
        }
        xTaskDelayUntil(&time, configTICK_RATE_HZ * angle_info.average_time_ms / 1000 / AVERAGING_BUFFER_SIZE);
    }
}

const char* sensor_status()
{
    switch (angle_info.status)
    {
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

_Noreturn void dev_service_task(__unused void* args)
{
    TickType_t time = xTaskGetTickCount();
    while (1)
    {
        const char* statusStr = sensor_status();
        ESP_LOGI(TAG_ANGLE,
                 "Status: %10s; AGC: x%02x; RAW ANGLE: %3d; ANGLE: %3d; AVERAGE_ANGLE: %3d; SPEED_TICKS: %d; SPEED: %f",
                 statusStr,
                 agc,
                 angle_info.last_raw_angle,
                 angle_info.last_angle,
                 angle_info.angle,
                 wind_speed_info.wind_ticks,
                 wind_speed_info.wind);

        vTaskDelayUntil(&time, configTICK_RATE_HZ)
    }
}

int sensor_response(char* buffer, ssize_t capacity)
{
    const char* statusStr = sensor_status();
    return snprintf(buffer, capacity, "{\"sensor\": \"%s\",\"agc\":%d,\"angle\":%d,\"speed\": %3.1f}", statusStr,
                     agc,
                     angle_info.angle, wind_speed_info.wind);
}
