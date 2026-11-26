#include <stdio.h>
#include <inttypes.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "display.h"
#include "hd44780.h"
#include "pcf8574.h"


#define I2C_ADDR										0x27												
#define I2C_MASTER_SDA									8
#define I2C_MASTER_SCL									1


static i2c_dev_t pcf8574;


static const uint8_t char_data[] =
{
    0x04, 0x0e, 0x0e, 0x0e, 0x1f, 0x00, 0x04, 0x00,
    0x1f, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x1f, 0x00
};

static esp_err_t write_lcd_data(const hd44780_t *lcd, uint8_t data)
{
    return pcf8574_port_write(&pcf8574, data);
}


void display_draw_home()
{
	
}
/*
void display_task(void *pvParameters)
{
    hd44780_t lcd =
    {
        .write_cb = write_lcd_data,		 // use callback to send data to LCD by I2C GPIO expander
        .font = HD44780_FONT_5X8,
        .lines = 2,
        .pins = {
            .rs = 0,
            .e  = 2,
            .d4 = 4,
            .d5 = 5,
            .d6 = 6,
            .d7 = 7,
            .bl = 3
        }
    };

    memset(&pcf8574, 0, sizeof(i2c_dev_t));
    ESP_ERROR_CHECK(pcf8574_init_desc(&pcf8574, I2C_ADDR, 1, I2C_MASTER_SDA, I2C_MASTER_SCL));

    ESP_ERROR_CHECK(hd44780_init(&lcd));

    //hd44780_switch_backlight(&lcd, true);

	
    hd44780_upload_character(&lcd, 0, char_data);
    hd44780_upload_character(&lcd, 1, char_data + 8);
    hd44780_gotoxy(&lcd, 0, 0);
    hd44780_puts(&lcd, "\x08 Hello world!");
    hd44780_gotoxy(&lcd, 0, 1);
    hd44780_puts(&lcd, "\x09 ");
	

    char time[16];

    for(;;)
    {
        hd44780_gotoxy(&lcd, 2, 1);
        snprintf(time, 7, "%" PRIu32 "  ", get_time_sec());
        time[sizeof(time) - 1] = 0;
        hd44780_puts(&lcd, time);

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
*/

void display_init()
{
    ESP_ERROR_CHECK(i2cdev_init());

	hd44780_t lcd =
    {
        .write_cb = write_lcd_data,		 // use callback to send data to LCD by I2C GPIO expander
        .font = HD44780_FONT_5X8,
        .lines = 2,
        .pins = {
            .rs = 0,
            .e  = 2,
            .d4 = 4,
            .d5 = 5,
            .d6 = 6,
            .d7 = 7,
            .bl = 3
        }
    };

    memset(&pcf8574, 0, sizeof(i2c_dev_t));
    ESP_ERROR_CHECK(pcf8574_init_desc(&pcf8574, I2C_ADDR, 1, I2C_MASTER_SDA, I2C_MASTER_SCL));

    ESP_ERROR_CHECK(hd44780_init(&lcd));
	
	hd44780_upload_character(&lcd, 0, char_data);
	hd44780_upload_character(&lcd, 1, char_data + 8);
	hd44780_gotoxy(&lcd, 0, 0);
	hd44780_puts(&lcd, "\x08 Hello world!");
	hd44780_gotoxy(&lcd, 0, 1);
	hd44780_puts(&lcd, "\x09 ");
}
