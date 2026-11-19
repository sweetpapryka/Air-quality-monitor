#include <stdio.h>

#include "freertos/FreeRTOS.h"

#include "esp_hmac.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "nvs_sec_provider.h"

#include "esp_log.h"

#include "nvs_component.h"


static const char TAG[] = "nvs_component";


SemaphoreHandle_t nvs_cmpnt_semaphore;


esp_err_t nvs_hmac_part_init(const char *label)
{
    esp_err_t ret = ESP_OK;
    nvs_sec_cfg_t cfg = {};
    nvs_sec_scheme_t *sec_scheme_handle = nvs_flash_get_default_security_scheme();

    ret = nvs_flash_read_security_cfg_v2(sec_scheme_handle, &cfg);
    ret = nvs_flash_secure_init_partition(label, &cfg);
	if(ret != ESP_OK)
	{
		return ret;
	}
    ESP_LOGI(TAG, "NVS partition \"%s\" initialized.", label);
    memset(&cfg, 0x00, sizeof(nvs_sec_cfg_t));

    return ret;
}
