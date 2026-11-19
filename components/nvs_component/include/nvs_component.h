#pragma once


#include "freertos/FreeRTOS.h"


/*
 *	Creates an encrypted NVS partition using HMAC	
 */
esp_err_t nvs_hmac_part_init(const char *label);
