#include <stdio.h>

#include "freertos/FreeRTOS.h"

#include "nvs_component.h"
#include "esp_hmac.h"
#include "freertos/idf_additions.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "nvs_sec_provider.h"


static const char TAG[] = "nvs_component";


SemaphoreHandle_t nvs_cmpnt_semaphore;


void nvs_cmpnt_init()
{
	ESP_ERROR_CHECK(nvs_flash_init());
	
	nvs_cmpnt_semaphore = xSemaphoreCreateMutex();
}

