#include "freertos/FreeRTOS.h"

#include "esp_log.h"

#include "bme688_device.h"
#include "pms5003.h"
#include "nvs_component.h"
#include "display.h"
#include "ui.h"


static const char TAG[] = "app_main";


void app_main()
{
	pms5003_create_task();
	bme688_device_create_task();
	display_init();
	ui_create_task();
	
	vTaskDelay(pdMS_TO_TICKS(10000));
	for(;;)
	{
		
	ESP_LOGI(TAG, "T=%.2f degC  P=%.2f Pa  RH=%.2f %%  gas=%.0f ohm (gas_valid=%d, heater_stable=%d)",
	         g_bme688_bsec_data.bme688_data.temperature, g_bme688_bsec_data.bme688_data.pressure, g_bme688_bsec_data.bme688_data.humidity, g_bme688_bsec_data.bme688_data.gas_resistance,
	         !!(g_bme688_bsec_data.bme688_data.status & BME68X_GASM_VALID_MSK),
	         !!(g_bme688_bsec_data.bme688_data.status & BME68X_HEAT_STAB_MSK));
	
			 		 
					 
	ESP_LOGI(TAG, "PMS 1.0u particles - %d", pms5003_data.pm1_data);
	ESP_LOGI(TAG, "PMS 2.5u particles - %d", pms5003_data.pm2_5_data);
	ESP_LOGI(TAG, "PMS 10.0u particles - %d", pms5003_data.pm10_data);
	
	vTaskDelay(pdMS_TO_TICKS(1000));
	}
}