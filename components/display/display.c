#include <stdint.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"
#include "hd44780.h"
#include "pcf8574.h"

#include "display.h"
#include "bme688_device.h"
#include "pms5003.h"
#include "portmacro.h"


#define I2C_ADDR										0x27												
#define I2C_MASTER_SDA									8
#define I2C_MASTER_SCL									1

#define DEGREE_SYMBOL_BITMAP							{0x0C, 0x12, 0x12, 0x0C, 0x00, 0x00, 0x00, 0x00}

#define LETTERS_FROM_RIGHT_SIDE														

typedef enum custom_characters{
	DEGREE_SYMBOL				// 0
}custom_characters_t;


static i2c_dev_t pcf8574;
static hd44780_t lcd;

static const uint8_t custom_characters[][8] =
{
    DEGREE_SYMBOL_BITMAP		// 0
};

static esp_err_t write_lcd_data(const hd44780_t *lcd, uint8_t data)
{
    return pcf8574_port_write(&pcf8574, data);
}

void display_backlight_on()
{
	hd44780_switch_backlight(&lcd, true);
}

void display_backlight_off()
{
	hd44780_switch_backlight(&lcd, false);
}

void display_draw_home()
{	
	hd44780_control(&lcd, true, false, false);
	
	xSemaphoreTake(g_bme688_bsec_data.mutex, portMAX_DELAY);
	bme688_bsec_data_t data = g_bme688_bsec_data;
	xSemaphoreGive(g_bme688_bsec_data.mutex);

	
	
	char text_buffer[16] = {0};
	
	// First line
	snprintf(text_buffer, sizeof(text_buffer), "IAQ: %d", g_bme688_bsec_data.IAQ);
	hd44780_puts(&lcd, text_buffer);
	
	hd44780_gotoxy(&lcd, 12, 0);
	snprintf(text_buffer, sizeof(text_buffer), "P1: %d", pms5003_data.pm1_data);
	hd44780_puts(&lcd, text_buffer);
	
	// Second line
	hd44780_gotoxy(&lcd, 0, 1);
	snprintf(text_buffer, sizeof(text_buffer), "Temp: %.1f", g_bme688_bsec_data.bme688_data.temperature);
	hd44780_puts(&lcd, text_buffer);
	
	hd44780_gotoxy(&lcd, 12, 1);
	snprintf(text_buffer, sizeof(text_buffer), "P2.5: %d", pms5003_data.pm2_5_data);
	hd44780_puts(&lcd, text_buffer);
	
	// Third line
	hd44780_gotoxy(&lcd, 0, 2);
	snprintf(text_buffer, sizeof(text_buffer), "Humid: %.1f", g_bme688_bsec_data.bme688_data.humidity);
	hd44780_puts(&lcd, text_buffer);
	
	hd44780_gotoxy(&lcd, 12, 2);
	snprintf(text_buffer, sizeof(text_buffer), "P10: %d", pms5003_data.pm10_data);
	hd44780_puts(&lcd, text_buffer);
		
	// Fourth line
	hd44780_gotoxy(&lcd, 0, 3);
	strlcpy(text_buffer, "Time: 12:34", sizeof(text_buffer));
	hd44780_puts(&lcd, text_buffer);
	
	hd44780_gotoxy(&lcd, 11, 3);
	strlcpy(text_buffer, ">", sizeof(text_buffer));
	hd44780_puts(&lcd, text_buffer);
}

void display_init()
{
    ESP_ERROR_CHECK(i2cdev_init());

	lcd = (hd44780_t ){
        .write_cb = write_lcd_data,		 // use callback to send data to LCD by I2C GPIO expander
        .font = HD44780_FONT_5X8,
        .lines = 4,
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
	
	hd44780_upload_character(&lcd, DEGREE_SYMBOL, custom_characters[0]);
	hd44780_switch_backlight(&lcd, true)
}
