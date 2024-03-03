#include "windsensor.h"
#include "driver/i2c.h"

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

void initAngleSensor() {
    ESP_LOGI(TAG_WIND, "Initialize i2c");
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
    ESP_LOGI(TAG_WIND, "i2c initialization done");
}

bool readAngle(volatile angle_info_t* angle_info)
{
    uint8_t buff[AS5600_OUT_REG_SIZE] = {AS5600_OUT_REG}; //Start reading from status, 18 bytes
    esp_err_t sensor_status = i2c_master_write_read_device(I2C_MASTER_PORT, AS560x_ADDR, buff, 1, buff,
                                                           sizeof(buff), 100);
    if (sensor_status != ESP_OK)
    {
        angle_info->status = ERROR;
        return false;
    }
    else
    {
        angle_info->agc = buff[AS5600_REG_AGC];
        uint8_t status = buff[AS5600_REG_STATUS];
        if (status & AS5600_STATUS_MD)
        {
            if (status & AS5600_STATUS_ML)
            {
                angle_info->status = FIELD_TOO_LOW;
            }
            else if (status & AS5600_STATUS_MH)
            {
                angle_info->status = FIELD_TOO_HIGH;
            }
            else
            {
                angle_info->status = OK;
            }
        }
        else
        {
            angle_info->status = NO_MAGNET;
        }
        angle_info->last_raw_angle = (buff[AS5600_REG_ANGLE_H] * 256 + buff[AS5600_REG_ANGLE_L]) * 360 / 4096;
        return true;
    }
}
