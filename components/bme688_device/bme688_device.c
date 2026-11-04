#include <stdio.h>
#include <string.h>
 
#include "freertos/FreeRTOS.h"

#include "driver/i2c_master.h"
#include "esp_log.h"

#include "bme688_device.h"
#include "bme68x.h"


#define BME_ADDRESS		0x76

#define I2C_PORT     	I2C_NUM_0
#define I2C_SDA_GPIO  	2
#define I2C_SCL_GPIO  	11
#define I2C_SPEED_HZ 	100000


static const char TAG[] = "bme688_device_component";

static i2c_master_bus_handle_t master_handle_i2c;
static i2c_master_dev_handle_t bme_handle_i2c;

struct bme68x_data bme688_data;
static struct bme68x_dev  bme_dev;
static struct bme68x_conf bme_conf;
static struct bme68x_heatr_conf bme_heatr_conf;


/* Bosch callbacks: return 0 on success, non-zero on failure */
static int8_t bme_read(uint8_t reg, uint8_t *data, uint32_t len, void *intf)
{
    esp_err_t err = i2c_master_transmit_receive(bme_handle_i2c, &reg, 1, data, len, 100);
    return (err == ESP_OK) ? 0 : -1;
}

static int8_t bme_write(uint8_t reg, const uint8_t *data, uint32_t len, void *intf)
{
	// Check if data len is as it should be
    uint8_t buf[32];
    if (len > sizeof(buf) - 1) {
        return -1;
    }
	// Append data to buff and transmit
    buf[0] = reg;
    memcpy(&buf[1], data, len);
    esp_err_t err = i2c_master_transmit(bme_handle_i2c, buf, len + 1, 100);
    return (err == ESP_OK) ? 0 : -1;
}

static void bme_delay_us(uint32_t us, void *intf)
{
    if (us >= 1000) {
		// Round to next milisecond as 1.5 would get rounded to 1 and delay would be too small
        TickType_t ticks = pdMS_TO_TICKS((us + 999) / 1000);
		// Safety net is someone changes tick-rate. f.e. tick-rate 100hz, 1 tick = 10ms means a 1 ms delay is rounded to 0. So wait alteast 1 tick.
        vTaskDelay(ticks ? ticks : 1);
    } else {
        esp_rom_delay_us(us);
    }
}

// Initialize the I2C master bus
static esp_err_t i2c_bus_init(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_PORT,
        .sda_io_num = I2C_SDA_GPIO,
        .scl_io_num = I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = false,
    };
    return i2c_new_master_bus(&bus_cfg, &master_handle_i2c);
}

// Initialize BME I2C and device descriptor
static esp_err_t bme_setup(void)
{
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BME_ADDRESS,
        .scl_speed_hz = I2C_SPEED_HZ,
    };
    esp_err_t err = i2c_master_bus_add_device(master_handle_i2c, &dev_cfg, &bme_handle_i2c);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "add_device failed: %s", esp_err_to_name(err));
        return err;
    }

    memset(&bme_dev, 0, sizeof(bme_dev));
    bme_dev.intf     = BME68X_I2C_INTF;
    bme_dev.read     = bme_read;
    bme_dev.write    = bme_write;
    bme_dev.delay_us = bme_delay_us;
    bme_dev.intf_ptr = NULL;
    bme_dev.amb_temp = 25;
    int8_t e = bme68x_init(&bme_dev);
    ESP_LOGI(TAG, "bme68x_init = %d (0 ok, -2 comm fail, -3 wrong chip id)", e);
    if (e != BME68X_OK) {
        return ESP_FAIL;
    }

    bme_conf.os_hum  = BME68X_OS_2X;
    bme_conf.os_pres = BME68X_OS_4X;
    bme_conf.os_temp = BME68X_OS_8X;
    bme_conf.filter  = BME68X_FILTER_OFF;
    bme_conf.odr     = BME68X_ODR_NONE;
    e = bme68x_set_conf(&bme_conf, &bme_dev);
    if (e != BME68X_OK) {
        ESP_LOGE(TAG, "bme68x_set_conf = %d", e);
        return ESP_FAIL;
    }

    bme_heatr_conf.enable     = BME68X_ENABLE;
    bme_heatr_conf.heatr_temp = 300;   /* Degrees C */
    bme_heatr_conf.heatr_dur  = 100;   /* ms */
    e = bme68x_set_heatr_conf(BME68X_FORCED_MODE, &bme_heatr_conf, &bme_dev);
    if (e != BME68X_OK) {
        ESP_LOGE(TAG, "bme68x_set_heatr_conf = %d", e);
        return ESP_FAIL;
    }

    return ESP_OK;
}

static void bme688_task(void *arg)
{
    for (;;) 
	{
		// Prompt sensor to measure
        int8_t rc = bme68x_set_op_mode(BME68X_FORCED_MODE, &bme_dev);
        if (rc != BME68X_OK) 
		{
            ESP_LOGE(TAG, "bme68x_set_op_mode failed! Retrying...");
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

		// Wait for measure
        uint32_t wait_us = bme68x_get_meas_dur(BME68X_FORCED_MODE, &bme_conf, &bme_dev) + (uint32_t)bme_heatr_conf.heatr_dur * 1000;
        bme_dev.delay_us(wait_us, bme_dev.intf_ptr);

		// Fetch data
        uint8_t data_len = 0;
        rc = bme68x_get_data(BME68X_FORCED_MODE, &bme688_data, &data_len, &bme_dev);
        if (rc != BME68X_OK || data_len > 0) 
		{
			ESP_LOGW(TAG, "bme68x_get_data failed");
        } 

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

void start_bme688(void)
{
    ESP_ERROR_CHECK(i2c_bus_init());
    
    if (bme_setup() == ESP_OK) {
        xTaskCreate(bme688_task, "bme688_task", 8192, NULL, 10, NULL);
    } else {
        abort();
    }
	
	ESP_LOGI(TAG, "Full BME setup and task creation complete");
}