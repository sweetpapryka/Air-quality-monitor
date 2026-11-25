#include <stdio.h>
#include <string.h>
 
#include "freertos/FreeRTOS.h"

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/idf_additions.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "bme68x.h"
#include "bme68x_defs.h"
#include "bsec_interface.h"
#include "bsec_datatypes.h"

#include "bme688_device.h"
#include "nvs_component.h"
              

#define BME_ADDRESS									0x76
	
#define I2C_PORT     								I2C_NUM_0
#define I2C_SDA_GPIO  								2
#define I2C_SCL_GPIO  								11
#define I2C_SPEED_HZ 								100000

#define MINUTES_BEFORE_STORING_BSEC_CONFIG			1 * (60 * 100000)
#define STORES_BEFORE_BACKUP						5 - 1					// -1 so that your number * MINUTES_BEFORE_STORING_BSEC_CONFIG		

#define NVS_BSEC_LABEL								"bsec_main"
#define NVS_BSEC_BACKUP_LABEL						"bsec_backup"
#define NVS_NAMESPACE								"bme688"
#define NVS_BSEC_CONFIG_KEY							"bsec_config"

#define ADD_INPUT(id, val) \ 
    in[n_in++] = (bsec_input_t){ .sensor_id = (id), .signal = (val), \
                                 .time_stamp = t, .signal_dimensions = 1 }


static const char TAG[] = "bme688_device_component";

static i2c_master_bus_handle_t master_handle_i2c;
static i2c_master_dev_handle_t bme_handle_i2c;
     
struct bme68x_data bme688_data;  
            
// Callback for the timer which triggers the task loop to store built-up BSEC config state on next iteration
static void timer_store_bsec_state_callback(void *arg)
{
	*(bool*) arg = true;
}

// Initialize the timer for storing BSEC state config
static void timer_store_bsec_state_init(esp_timer_handle_t *timer, bool *store_bsec_state)
{
	esp_timer_create_args_t config = {
		.callback = timer_store_bsec_state_callback,
		.arg = store_bsec_state,
		.name = "bsec_timer"
	};
	
	ESP_ERROR_CHECK(esp_timer_create(&config, timer));
}

// delete if nvs checksum failed

// Initialize the NVS partitions
static esp_err_t nvs_init()
{
	esp_err_t ret = ESP_OK;

	ret = nvs_hmac_part_init(NVS_BSEC_LABEL);
	if(ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
	{
		ESP_ERROR_CHECK(nvs_flash_erase_partition(NVS_BSEC_LABEL));
		nvs_hmac_part_init(NVS_BSEC_LABEL);
	}
	
	ret = nvs_hmac_part_init(NVS_BSEC_BACKUP_LABEL);
	if(ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
	{
		ESP_ERROR_CHECK(nvs_flash_erase_partition(NVS_BSEC_BACKUP_LABEL));
		nvs_hmac_part_init(NVS_BSEC_BACKUP_LABEL);
	}

	return ESP_OK;
}

// Write the built up BSEC config state to NVS
static esp_err_t nvs_write_bsec_config(uint8_t *serialized_state, uint32_t n_serialized_state, char partition_name[], char key[])
{
	nvs_handle_t nvs_handle;
	esp_err_t ret = ESP_OK;

	// Open NVS namespace
	ret = nvs_open_from_partition(partition_name, NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
	if (ret != ESP_OK) return ret;

	// Write
	ret = nvs_set_blob(nvs_handle, key, serialized_state, (size_t)n_serialized_state);
	if (ret != ESP_OK) return ret;
	
	ret = nvs_commit(nvs_handle);
	if (ret != ESP_OK) return ret;
	
	return ret;
}

// Read the built up BSEC config state from NVS
static esp_err_t nvs_read_bsec_config(uint8_t *serialized_state, uint32_t *n_serialized_state, char partition_name[], char key[])
{
	nvs_handle_t nvs_handle;
	esp_err_t ret = ESP_OK;

	// Open NVS namespace
	ret = nvs_open_from_partition(partition_name, NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
	if (ret != ESP_OK) return ret;
	
	// Write
	size_t len = BSEC_MAX_STATE_BLOB_SIZE;
	ret = nvs_get_blob(nvs_handle, key, serialized_state, &len);
	if (ret != ESP_OK) return ret;

	*n_serialized_state = len;
	
	return ret;
}

static int64_t now_ns(void) { return esp_timer_get_time() * 1000; }

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
static esp_err_t bme_setup(struct bme68x_dev *bme_dev)
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

    memset(bme_dev, 0, sizeof(*bme_dev));
    bme_dev->intf     = BME68X_I2C_INTF;
    bme_dev->read     = bme_read;
    bme_dev->write    = bme_write;
    bme_dev->delay_us = bme_delay_us;
    bme_dev->intf_ptr = NULL;
    bme_dev->amb_temp = 25;
    int8_t e = bme68x_init(bme_dev);
    ESP_LOGI(TAG, "bme68x_init = %d (0 ok, -2 comm fail, -3 wrong chip id)", e);
    if (e != BME68X_OK) {
        return ESP_FAIL;
    }

    return ESP_OK;
}

// Initialize BSEC and its subscriptions. Retrieve built-up state from NVS
static esp_err_t bsec_setup(uint8_t work_buffer[])
{
	static const uint8_t bsec_config[] = { 0,1,6,2,189,1,0,0,0,0,0,0,127,7,0,0,56,0,1,0,0,192,168,71,64,49,119,76,0,0,97,69,0,0,97,69,10,0,3,0,0,0,96,64,23,183,209,56,43,24,149,60,140,74,106,188,43,24,149,60,216,129,243,190,151,255,80,190,216,129,243,190,8,0,2,0,0,0,72,66,16,0,3,0,10,215,163,60,10,215,35,59,10,215,35,59,13,0,5,0,0,0,0,0,100,35,41,29,86,88,0,9,0,229,208,34,62,0,0,0,0,0,0,0,0,218,27,156,62,225,11,67,64,0,0,160,64,0,0,0,0,0,0,0,0,94,75,72,189,93,254,159,64,66,62,160,191,0,0,0,0,0,0,0,0,33,31,180,190,138,176,97,64,65,241,99,190,0,0,0,0,0,0,0,0,167,121,71,61,165,189,41,192,184,30,189,64,12,0,10,0,0,0,0,0,0,0,0,0,45,5,11,0,1,1,2,229,161,18,62,67,88,130,65,73,204,23,191,102,147,170,191,189,189,27,62,239,200,204,63,20,137,187,63,203,12,84,191,210,7,77,63,254,52,0,191,121,207,169,192,151,104,42,64,33,89,1,193,80,106,189,192,48,163,254,64,238,150,251,192,238,216,142,190,249,67,192,192,176,135,232,64,158,140,71,64,255,166,229,192,103,71,184,64,0,0,0,0,0,0,0,0,74,240,87,64,92,27,90,193,70,167,223,191,13,136,1,64,250,113,254,190,145,195,139,64,163,223,166,64,105,165,190,192,55,127,191,64,231,201,194,191,96,133,58,63,24,143,220,63,155,191,143,190,156,34,61,190,50,149,231,188,163,169,22,63,171,4,118,191,192,225,137,190,250,7,212,58,100,102,131,61,194,148,137,190,210,246,160,192,134,8,231,62,49,112,15,63,173,31,197,190,40,14,177,191,104,20,184,62,70,60,187,190,252,89,33,63,16,141,241,190,220,110,201,62,9,233,91,65,57,192,57,191,160,104,19,191,193,45,250,62,194,145,165,63,150,47,3,64,189,149,70,63,79,207,130,191,209,77,20,62,92,186,62,192,131,185,59,64,195,129,22,65,192,234,146,63,204,211,64,64,229,255,208,192,107,250,54,192,205,86,224,64,149,190,209,191,241,85,252,64,14,156,157,63,106,99,70,191,228,29,171,192,44,220,253,191,193,186,39,192,81,141,175,64,57,226,147,64,143,185,4,192,37,167,55,64,123,3,25,192,123,7,177,63,246,78,17,191,242,198,23,191,67,234,114,58,128,208,92,62,147,217,183,64,9,23,137,64,9,97,226,192,39,221,109,191,190,147,44,192,157,38,102,188,97,195,36,65,163,162,180,192,45,126,80,192,29,243,221,192,179,178,128,64,93,156,178,191,17,204,149,192,161,237,3,193,21,116,158,192,145,181,33,192,189,179,34,191,118,101,255,63,46,246,170,60,234,182,169,190,31,179,118,192,220,176,167,192,101,125,128,64,83,228,200,191,182,227,24,63,26,61,94,62,212,103,83,190,125,86,67,64,153,7,95,63,116,244,185,64,216,123,205,192,31,177,129,191,72,174,225,64,20,215,101,64,128,148,135,64,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,107,89,245,62,157,158,21,192,136,132,59,189,40,39,121,62,140,164,194,191,188,141,142,65,82,137,62,191,249,207,111,64,87,59,115,63,123,150,202,192,178,61,215,63,43,7,9,64,151,11,24,191,157,100,160,191,115,56,147,192,94,180,20,63,32,91,9,191,153,22,13,63,107,88,59,63,118,74,151,64,160,164,220,192,102,216,212,61,169,226,179,64,1,194,57,63,102,11,65,193,24,52,46,64,84,197,25,64,186,232,237,190,229,75,133,63,155,141,206,63,144,135,216,64,60,223,207,64,201,102,255,192,188,40,182,192,64,244,5,192,183,53,51,192,255,94,43,64,171,59,100,192,195,43,129,192,137,160,223,64,182,50,107,64,67,189,145,62,167,210,150,64,60,41,2,64,234,113,146,193,100,138,176,64,54,115,11,64,226,124,4,192,189,118,121,64,1,71,71,192,164,210,201,192,19,164,7,65,91,203,62,192,102,42,231,192,240,85,143,64,195,67,242,192,2,177,84,191,188,82,32,193,250,92,201,192,1,4,25,63,88,68,180,189,14,76,163,192,237,172,198,63,30,48,202,64,33,201,170,190,223,211,209,192,185,97,27,64,251,253,143,64,38,66,200,64,136,37,198,190,160,147,123,63,56,213,93,191,108,208,78,192,206,113,34,191,89,226,221,64,243,29,143,191,45,75,95,192,84,45,189,62,61,195,84,63,21,66,167,191,160,199,53,63,169,252,67,192,111,162,80,63,253,217,251,64,229,80,105,65,185,247,69,64,160,55,19,192,131,7,153,64,10,79,106,193,106,214,231,192,49,97,42,65,200,80,72,191,80,52,251,192,181,100,195,63,129,94,50,65,4,28,45,193,65,149,81,192,218,111,228,192,107,49,193,192,194,86,205,192,250,216,204,64,235,29,243,192,0,0,0,0,0,0,0,0,91,111,224,64,193,216,6,193,0,0,0,0,0,0,0,0,120,54,184,65,150,49,179,193,0,0,0,0,0,0,0,0,145,38,103,65,22,201,107,193,0,0,0,0,0,0,0,0,147,148,166,192,193,15,175,64,0,0,0,0,0,0,0,0,193,118,33,65,165,90,27,193,0,0,0,0,0,0,0,0,152,207,20,193,65,35,41,65,0,0,0,0,0,0,0,0,11,210,236,192,128,52,229,64,0,0,0,0,0,0,0,0,197,147,247,192,122,12,5,65,0,0,0,0,0,0,0,0,165,41,33,65,126,226,25,193,0,0,0,0,0,0,0,0,10,10,2,136,90,181,73,144,228,157,76,177,62,152,76,153,116,144,76,211,67,146,75,18,17,140,75,252,189,134,75,210,72,170,73,73,128,180,73,169,107,186,73,0,0,0,0,0,0,0,0,0,0,0,0,103,149,96,73,67,91,251,75,154,82,4,76,138,187,12,76,99,43,138,75,11,54,134,75,214,136,130,75,52,217,85,73,132,245,92,73,140,146,97,73,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,145,1,254,0,2,1,5,48,117,100,0,44,1,112,23,151,7,132,3,197,0,92,4,144,1,64,1,64,1,144,1,48,117,48,117,48,117,48,117,100,0,100,0,100,0,48,117,48,117,48,117,100,0,100,0,48,117,48,117,8,7,8,7,8,7,8,7,8,7,8,7,8,7,8,7,8,7,100,0,100,0,100,0,100,0,48,117,48,117,48,117,100,0,100,0,100,0,48,117,48,117,100,0,100,0,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,44,1,44,1,44,1,44,1,44,1,44,1,44,1,44,1,44,1,44,1,44,1,44,1,44,1,44,1,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,112,23,112,23,112,23,112,23,8,7,8,7,8,7,8,7,112,23,112,23,112,23,112,23,112,23,112,23,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,112,23,112,23,112,23,112,23,255,255,255,255,220,5,220,5,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,220,5,220,5,220,5,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,44,1,0,5,10,5,0,2,0,10,0,30,0,5,0,5,0,5,0,5,0,5,0,5,0,64,1,100,0,100,0,100,0,200,0,200,0,200,0,64,1,64,1,64,1,10,1,0,0,0,0,0,139,147,0,0 };
	
	bsec_init();
		
	bsec_set_configuration(bsec_config, sizeof(bsec_config), work_buffer, BSEC_MAX_WORKBUFFER_SIZE);		
	
	// BSEC state config variables to get/reset state from built up data
	uint8_t  serialized_state[BSEC_MAX_STATE_BLOB_SIZE] = {0};
	uint32_t n_serialized_state = 0;	

	// Restore BSEC state if NVS data was damaged from shutdown read BACKUP
	if(nvs_read_bsec_config(serialized_state, &n_serialized_state, NVS_BSEC_LABEL,NVS_BSEC_CONFIG_KEY) == ESP_OK && 0)
	{
		if(bsec_set_state(serialized_state, n_serialized_state, work_buffer, BSEC_MAX_WORKBUFFER_SIZE) == BSEC_OK)
			ESP_LOGI(TAG, "READ & APPLIED BSEC STATE FROM NVS");
	}
	else 
	{
		if(nvs_read_bsec_config(serialized_state, &n_serialized_state, NVS_BSEC_BACKUP_LABEL,NVS_BSEC_CONFIG_KEY) == ESP_OK)
		{
			if(bsec_set_state(serialized_state, n_serialized_state, work_buffer, BSEC_MAX_WORKBUFFER_SIZE) == BSEC_OK)
				ESP_LOGI(TAG, "READ & APPLIED BACKUP BSEC STATE FROM NVS");
		}
		
	}
	
	bsec_sensor_configuration_t subcriptions [] = {
		{.sensor_id = BSEC_OUTPUT_IAQ, .sample_rate = BSEC_SAMPLE_RATE_LP}
	};
	bsec_sensor_configuration_t sensors_used[BSEC_MAX_PHYSICAL_SENSOR];
	uint8_t size = BSEC_MAX_PHYSICAL_SENSOR;
	bsec_update_subscription(subcriptions, sizeof(subcriptions) / sizeof(subcriptions[0]), sensors_used, &size);
	
	return ESP_OK;
}

static void bme688_task(void *arg)
{
	esp_timer_handle_t timer = {0};
	
	struct bme68x_dev bme_dev = {0};
	static uint8_t work_buffer[BSEC_MAX_WORKBUFFER_SIZE] = {0};
	
	// Variables to store the built-up BSEC state config in NVS
	uint8_t  serialized_state[BSEC_MAX_STATE_BLOB_SIZE] = {0};
	uint32_t n_serialized_state = 0;
	bool store_bsec_config = false;
	uint16_t loops_before_backup = STORES_BEFORE_BACKUP;
	
	timer_store_bsec_state_init(&timer, &store_bsec_config);
	esp_timer_start_periodic(timer, MINUTES_BEFORE_STORING_BSEC_CONFIG);
	bme_setup(&bme_dev);
	bsec_setup(work_buffer);

    for (;;) 
	{	
		bsec_bme_settings_t bme_settings;
        int64_t t = now_ns();
        bsec_sensor_control(t, &bme_settings);
		
		if(bme_settings.trigger_measurement)
		{
			static struct bme68x_conf bme_conf;
			static struct bme68x_heatr_conf bme_heatr_conf;
			uint8_t n_fields = 0;
			
			bme68x_get_conf(&bme_conf, &bme_dev);
			bme_conf.os_hum  = bme_settings.humidity_oversampling;
			bme_conf.os_pres = bme_settings.pressure_oversampling;
			bme_conf.os_temp = bme_settings.temperature_oversampling;
			bme68x_set_conf(&bme_conf, &bme_dev);
			
			bme_heatr_conf.enable     = bme_settings.run_gas ? BME68X_ENABLE : BME68X_DISABLE;
			bme_heatr_conf.heatr_temp = bme_settings.heater_temperature;   /* Degrees C */
			bme_heatr_conf.heatr_dur  = bme_settings.heater_duration;   /* ms */
			bme68x_set_heatr_conf(BME68X_FORCED_MODE, &bme_heatr_conf, &bme_dev);
			// Prompt sensor to measure
			bme68x_set_op_mode(BME68X_FORCED_MODE, &bme_dev);
			// Wait for measure
		    uint32_t wait_us = bme68x_get_meas_dur(BME68X_FORCED_MODE, &bme_conf, &bme_dev) + (uint32_t)bme_heatr_conf.heatr_dur * 1000;
		    bme_dev.delay_us(wait_us, bme_dev.intf_ptr);
			if (bme68x_get_data(BME68X_FORCED_MODE, &bme688_data, &n_fields, &bme_dev) == BME68X_OK && n_fields > 0)
			{
				bsec_input_t in[4];
				uint8_t n_in = 0;
				
				if (bme_settings.process_data & BSEC_PROCESS_TEMPERATURE) ADD_INPUT(BSEC_INPUT_TEMPERATURE, bme688_data.temperature);
				if (bme_settings.process_data & BSEC_PROCESS_HUMIDITY)    ADD_INPUT(BSEC_INPUT_HUMIDITY,    bme688_data.humidity);
				if (bme_settings.process_data & BSEC_PROCESS_PRESSURE)    ADD_INPUT(BSEC_INPUT_PRESSURE,    bme688_data.pressure);
				if ((bme_settings.process_data & BSEC_PROCESS_GAS) && (bme688_data.status & BME68X_GASM_VALID_MSK))
				    ADD_INPUT(BSEC_INPUT_GASRESISTOR, bme688_data.gas_resistance);
			
				bsec_output_t out[BSEC_NUMBER_OUTPUTS];
				uint8_t n_out = BSEC_NUMBER_OUTPUTS;
				if (bsec_do_steps(in, n_in, out, &n_out) >= BSEC_OK) {
				    for (int i = 0; i < n_out; i++) {
				        if (out[i].sensor_id == BSEC_OUTPUT_IAQ)
				            printf("IAQ %.1f (accuracy %d)\n", out[i].signal, out[i].accuracy);
				    }
				}
			}
		}
		
		if(store_bsec_config)
		{
			if(bsec_get_state(0, serialized_state, BSEC_MAX_STATE_BLOB_SIZE, work_buffer, BSEC_MAX_WORKBUFFER_SIZE, &n_serialized_state) == BSEC_OK && n_serialized_state > 0)
			{
				// Write current state and update backup
				if(nvs_write_bsec_config(serialized_state, n_serialized_state, NVS_BSEC_LABEL, NVS_BSEC_CONFIG_KEY) == ESP_OK)
				{
					if(loops_before_backup == 0)
					{
						if(nvs_write_bsec_config(serialized_state, n_serialized_state, NVS_BSEC_BACKUP_LABEL, NVS_BSEC_CONFIG_KEY) == ESP_OK)
						{
							ESP_LOGI(TAG, "WROTE BACKUP BSEC STATE TO NVS");
							loops_before_backup = STORES_BEFORE_BACKUP + 1;
						}
					}
					store_bsec_config = false;
					ESP_LOGI(TAG, "WROTE BSEC STATE TO NVS");
				}
				else 
				{
					ESP_LOGW(TAG, "WRITE BSEC STATE TO NVS FAILED!");
				}
			}
			--loops_before_backup;
		}
		
		int64_t wait_ms = (bme_settings.next_call - now_ns()) / 1000000;
		// Add 1ms to compensate for rounding
		// 10ms = 1 FreeRTOS tick. If delay is less than 10ms wait atleast 1 tick
		if (wait_ms >= 10) vTaskDelay(pdMS_TO_TICKS(wait_ms + 1));
		else if (wait_ms < 10 && wait_ms > 0) vTaskDelay(1);
    }
}

void bme688_device_create_task(void)
{
    ESP_ERROR_CHECK(i2c_bus_init());
	ESP_ERROR_CHECK(nvs_init());
    
    xTaskCreatePinnedToCore(bme688_task, "bme688_task", 8192, NULL, 3, NULL, PRO_CPU_NUM);

	ESP_LOGI(TAG, "BME688 task creation complete");
}