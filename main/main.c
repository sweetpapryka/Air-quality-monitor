#include "freertos/FreeRTOS.h"

#include "esp_log.h"

#include "bme688_device.h"
#include "pms5003.h"


static const char TAG[] = "app_main";


void app_main()
{
	start_pms5003();
	start_bme688();
	
	
	vTaskDelay(pdMS_TO_TICKS(10000));
	for(;;)
	{
	ESP_LOGI(TAG, "T=%.2f degC  P=%.2f Pa  RH=%.2f %%  gas=%.0f ohm (gas_valid=%d, heater_stable=%d)",
	         bme688_data.temperature, bme688_data.pressure, bme688_data.humidity, bme688_data.gas_resistance,
	         !!(bme688_data.status & BME68X_GASM_VALID_MSK),
	         !!(bme688_data.status & BME68X_HEAT_STAB_MSK));
			 
	ESP_LOGI(TAG, "PMS 1.0u particles - %d", pms5003_data.pm1_data);
	ESP_LOGI(TAG, "PMS 2.5u particles - %d", pms5003_data.pm2_5_data);
	ESP_LOGI(TAG, "PMS 10.0u particles - %d", pms5003_data.pm10_data);
	vTaskDelay(pdMS_TO_TICKS(1000));
	}
}