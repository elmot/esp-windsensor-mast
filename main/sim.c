#include "windsensor.h"
#include "driver/gpio.h"

#define SIM_PIN GPIO_NUM_5

_Noreturn void wind_freq_generator(void* args)
{
    //zero-initialize the config structure.
    TickType_t time = xTaskGetTickCount();

    const gpio_config_t io_conf = {
        //bit mask of the pins that you want to set,e.g.GPIO18/19
        .pin_bit_mask = BIT64(SIM_PIN),
        //disable interrupt
        .intr_type = GPIO_INTR_DISABLE,
        //set as output mode
        .mode = GPIO_MODE_OUTPUT,
        //disable pull-down mode
        .pull_down_en = 0,
        //disable pull-up mode
        .pull_up_en = 0
    };
    gpio_config(&io_conf);
    const int wind_ticks[5] = {configTICK_RATE_HZ/2,
        configTICK_RATE_HZ/4,
        configTICK_RATE_HZ/2,
        configTICK_RATE_HZ/5,
        configTICK_RATE_HZ/3};

    for (int loopCounter = 0; true; loopCounter = (loopCounter + 1) % 50)
    {
        int ticks = wind_ticks[loopCounter /10];
        gpio_set_level(SIM_PIN, 0);
        vTaskDelayUntil(&time, ticks);
        gpio_set_level(SIM_PIN, 1);
        vTaskDelayUntil(&time, ticks);
    }
}
