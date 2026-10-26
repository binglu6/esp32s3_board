#include "bsp_lvgl_display.h"

static const char *TAG = "bsp_lvgl";
/* LCD IO and panel */
static esp_lcd_touch_handle_t touch_handle = NULL;

/* LVGL display and touch */
static lv_display_t *lvgl_disp = NULL;
static lv_indev_t *lvgl_touch_indev = NULL;

static esp_err_t app_touch_init(void)
{
    /* Initilize I2C */
    // i2c_master_bus_handle_t i2c_handle = NULL;
    // const i2c_master_bus_config_t i2c_config = {
    //     .i2c_port = BSP_I2C_NUM,
    //     .sda_io_num = BSP_I2C_SDA,
    //     .scl_io_num = BSP_I2C_SCL,
    //     .clk_source = I2C_CLK_SRC_DEFAULT,
    // };
    // ESP_RETURN_ON_ERROR(i2c_new_master_bus(&i2c_config, &i2c_handle), TAG, "");

    /* Initialize touch HW */
    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = BSP_LCD_V_RES,
        .y_max = BSP_LCD_H_RES,
        .rst_gpio_num = GPIO_NUM_NC, // Shared with LCD reset
        .int_gpio_num = GPIO_NUM_NC,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 1,
            .mirror_x = 0,
            .mirror_y = 1,
        },
    };
    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_FT5x06_CONFIG();
    tp_io_config.scl_speed_hz = EXAMPLE_TOUCH_I2C_CLK_HZ;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(i2c_bus_handle, &tp_io_config, &tp_io_handle), TAG, "");
    return esp_lcd_touch_new_i2c_ft5x06(tp_io_handle, &tp_cfg, &touch_handle);
}

static esp_err_t app_lvgl_init(void)
{
    /* Initialize LVGL */
    const lvgl_port_cfg_t lvgl_cfg = {
        .task_priority = 4,         /* LVGL task priority */
        .task_stack = 8192,         /* LVGL task stack size */ 
        .task_affinity = -1,        /* LVGL task pinned to core (-1 is no affinity) */
        .task_max_sleep_ms = 500,   /* Maximum sleep in LVGL task */
        .timer_period_ms = 5        /* LVGL timer tick period in ms */
    };
    
    ESP_RETURN_ON_ERROR(lvgl_port_init(&lvgl_cfg), TAG, "LVGL port initialization failed");

    /* Add LCD screen */
    ESP_LOGD(TAG, "Add LCD screen");
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = lcd_io,
        .panel_handle = lcd_panel,
        .buffer_size = BSP_LCD_H_RES * LCD_DRAW_BUFF_HEIGHT,
        .double_buffer = LCD_DRAW_BUFF_DOUBLE,
        .hres = BSP_LCD_H_RES,
        .vres = BSP_LCD_V_RES,
        .monochrome = false,
#if LVGL_VERSION_MAJOR >= 9
        .color_format = LV_COLOR_FORMAT_RGB565,
#endif
        .rotation = {
            .swap_xy = true,
            .mirror_x = true,
            .mirror_y = true,
        },
        .flags = {
            .buff_dma = true,
#if LVGL_VERSION_MAJOR >= 9
            .swap_bytes = true,
#endif
        }
    };
    lvgl_disp = lvgl_port_add_disp(&disp_cfg);

    /* Add touch input (for selected screen) */
    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp = lvgl_disp,
        .handle = touch_handle,
    };
    lvgl_touch_indev = lvgl_port_add_touch(&touch_cfg);

    return ESP_OK;
}

void my_display(void)
{
    /* Task lock */
    lvgl_port_lock(0);
    
    lv_obj_t *obj1 = NULL; // 初始化为 NULL（可选，但推荐）
    lv_obj_t *switch1 = NULL;
    lv_obj_t *lv_label1 = NULL;

    static lv_style_t style;
    lv_style_init(&style);
    lv_style_set_bg_color(&style, lv_color_hex(0xfb3d3d));

    obj1 = lv_obj_create(lv_scr_act());
    lv_obj_set_size(obj1, 200, 200);
    lv_obj_set_style_bg_color(obj1, lv_color_make(255, 0, 0), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(obj1, lv_color_make(0, 255, 0), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(obj1, lv_color_make(0, 0, 255), LV_STATE_DEFAULT);

    switch1 = lv_switch_create(obj1);
    lv_obj_set_size(switch1, 100, 50);
    // lv_obj_set_pos(switch1,300,0);
    lv_obj_center(switch1);
    
    lv_obj_add_style(switch1, &style, LV_STATE_DEFAULT);

    lv_label1 = lv_label_create(lv_scr_act());
    lv_label_set_text(lv_label1, "hello world! rgb(0, 255, 0) LVGL");
    lv_obj_set_style_text_color(lv_label1, lv_color_make(0, 255, 0), LV_STATE_DEFAULT);
    // lv_obj_set_style_text_font(lv_label1, &lv_font_montserrat_28, LV_STATE_DEFAULT);
    lv_obj_align(lv_label1, LV_ALIGN_CENTER, 0, 0);
    // lv_label_set_recolor(lv_label1,true); lvgl v9 miss this!
    // lv_obj_add_event_cb(switch1, my_event_cb, LV_EVENT_CLICKED, NULL);
    // lv_obj_add_event_cb(obj1, my_event_cb, LV_EVENT_CLICKED, NULL);
    // lv_obj_add_event_cb(obj1, my_event_cb, LV_EVENT_LONG_PRESSED, NULL);

    /* Task unlock */
    lvgl_port_unlock();
}

// 开发板显示初始化
void bsp_lvgl_start(void)
{
    /* LCD HW initialization */
    ESP_ERROR_CHECK(bsp_lcd_init());
    /* Touch + LVGL initialization */
    ESP_ERROR_CHECK(bsp_lvgl_display_init());

    /* Show LVGL objects */
    lv_demo_music();
    // my_display();
}

// 仅初始化触摸与 LVGL 显示（不运行任何 demo），供其它模块复用
esp_err_t bsp_lvgl_display_init(void)
{
    /* Touch initialization */
    ESP_RETURN_ON_ERROR(app_touch_init(), TAG, "Touch init failed");

    /* LVGL initialization */
    ESP_RETURN_ON_ERROR(app_lvgl_init(), TAG, "LVGL init failed");

    return ESP_OK;
}

