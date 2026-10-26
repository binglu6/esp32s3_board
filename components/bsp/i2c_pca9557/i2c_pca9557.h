// i2c_pca9557.h
#ifndef I2C_PCA9557_H  // 防止头文件重复包含
#define I2C_PCA9557_H

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "logprint.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
// #include "driver/i2c.h" //旧版
// 新驱动代码
#include "driver/i2c_master.h"  // 引入新版头文件


/* I2C port and GPIOs */
#define BSP_I2C_NUM         (0)
#define BSP_I2C_SDA         (1)
#define BSP_I2C_SCL         (2)
#define BSP_I2C_FREQ_HZ       100000

#define PCA9557_INPUT_PORT              0x00
#define PCA9557_OUTPUT_PORT             0x01
#define PCA9557_POLARITY_INVERSION_PORT 0x02
#define PCA9557_CONFIGURATION_PORT      0x03

#define LCD_CS_GPIO                 BIT(0)    // PCA9557_GPIO_NUM_1
#define PA_EN_GPIO                  BIT(1)    // PCA9557_GPIO_NUM_2
#define DVP_PWDN_GPIO               BIT(2)    // PCA9557_GPIO_NUM_3
#define SD_CS_GPIO                  BIT(3)    // PCA9557_GPIO_NUM_4

#define PCA9557_SENSOR_ADDR             0x19        /*!< Slave address of the MPU9250 sensor */

#define SET_BITS(_m, _s, _v)  ((_v) ? (_m)|((_s)) : (_m)&~((_s)))

void pca9557_init(void);
void lcd_cs(uint8_t level);
void pa_en(uint8_t level);
void dvp_pwdn(uint8_t level);
void sd_cs(uint8_t level);
esp_err_t bsp_i2c_init(void);
extern i2c_master_bus_handle_t i2c_bus_handle;

#ifdef __cplusplus
}
#endif

#endif // i2c_pca9557.h