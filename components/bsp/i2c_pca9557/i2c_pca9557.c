#include "i2c_pca9557.h"

//new i2c
i2c_master_bus_handle_t i2c_bus_handle = NULL;
static i2c_master_dev_handle_t pca9557_handle = NULL;

static uint8_t pca9557_register_read(uint8_t reg_addr)
{
    uint8_t data;
    i2c_master_transmit_receive(pca9557_handle, &reg_addr, 1, &data, 1, 1000 / portTICK_PERIOD_MS);
    return data;
}

esp_err_t pca9557_register_write_byte(uint8_t reg_addr, uint8_t data)
{
    i2c_device_config_t dev_conf = {
        .device_address = PCA9557_SENSOR_ADDR,
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .scl_speed_hz = BSP_I2C_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus_handle, &dev_conf, &pca9557_handle)); // 添加设备到总线

    uint8_t write_buf[2] = {reg_addr, data};
    return i2c_master_transmit(pca9557_handle, write_buf, sizeof(write_buf), 1000 / portTICK_PERIOD_MS);
}
//new i2c

//old i2c
// static uint8_t pca9557_register_read(uint8_t reg_addr)
// {
//     uint8_t data;
//     i2c_master_write_read_device(BSP_I2C_NUM, PCA9557_SENSOR_ADDR, &reg_addr, 1, &data, 1, 1000 / portTICK_PERIOD_MS);
//     return data;
// }

// esp_err_t pca9557_register_write_byte(uint8_t reg_addr, uint8_t data)
// {
//     int ret;
//     uint8_t write_buf[2] = {reg_addr, data};

//     ret = i2c_master_write_to_device(BSP_I2C_NUM, PCA9557_SENSOR_ADDR, write_buf, sizeof(write_buf), 1000 / portTICK_PERIOD_MS);

//     return ret;
// }
//old i2c

void pca9557_init(void)
{
    // 写入控制引脚默认值 SD_CS = 1 DVP_PWDN=1  PA_EN = 0  LCD_CS = 1
    pca9557_register_write_byte(PCA9557_OUTPUT_PORT, 0x0D); 
    // 把PCA9557芯片的IO0 IO1 IO2 IO3设置为输出 其它引脚保持默认的输入 1为输入，0为输出
    pca9557_register_write_byte(PCA9557_CONFIGURATION_PORT, 0xF0);
}

// 设置PCA9557芯片的某个IO引脚输出高低电平
esp_err_t pca9557_set_output_state(uint8_t gpio_bit, uint8_t level)
{
    char data;
    esp_err_t res = ESP_FAIL;

    data = pca9557_register_read(PCA9557_OUTPUT_PORT);
    res = pca9557_register_write_byte(PCA9557_OUTPUT_PORT, SET_BITS(data, gpio_bit, level));

    return res;
}

// 控制 PCA9557_LCD_CS 引脚输出高低电平 参数0输出低电平 参数1输出高电平
void lcd_cs(uint8_t level)
{
    pca9557_set_output_state(LCD_CS_GPIO, level);
}

// 控制 PCA9557_PA_EN 引脚输出高低电平 参数0输出低电平 参数1输出高电平
void pa_en(uint8_t level)
{
    pca9557_set_output_state(PA_EN_GPIO, level);
}

// 控制 PCA9557_DVP_PWDN 引脚输出高低电平 参数0输出低电平 参数1输出高电平
void dvp_pwdn(uint8_t level)
{
    pca9557_set_output_state(DVP_PWDN_GPIO, level);
}

// 控制 PCA9557_SD_CS 引脚输出高低电平 参数0输出低电平 参数1输出高电平
void sd_cs(uint8_t level)
{
    pca9557_set_output_state(SD_CS_GPIO, level);
}

//new i2c
esp_err_t bsp_i2c_init(void)
{
    i2c_master_bus_config_t bus_conf = {
        .i2c_port = BSP_I2C_NUM,
        .sda_io_num = BSP_I2C_SDA,
        .scl_io_num = BSP_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true, // 启用内部上拉
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_conf, &i2c_bus_handle)); // 创建总线

    return ESP_OK;
}


//old i2c
// esp_err_t bsp_i2c_init(void)
// {
//     i2c_config_t i2c_conf = {
//         .mode = I2C_MODE_MASTER,
//         .sda_io_num = BSP_I2C_SDA,
//         .sda_pullup_en = GPIO_PULLUP_ENABLE,
//         .scl_io_num = BSP_I2C_SCL,
//         .scl_pullup_en = GPIO_PULLUP_ENABLE,
//         .master.clk_speed = BSP_I2C_FREQ_HZ
//     };
//     i2c_param_config(BSP_I2C_NUM, &i2c_conf);

//     return i2c_driver_install(BSP_I2C_NUM, i2c_conf.mode, 0, 0, 0);
// }

