#pragma once


#include <inttypes.h>

#include "bme68x.h"
#include "freertos/idf_additions.h"


typedef struct bme688_bsec_data{
	struct bme68x_data bme688_data;
	uint16_t IAQ;
	SemaphoreHandle_t mutex;
}bme688_bsec_data_t;


extern bme688_bsec_data_t g_bme688_bsec_data;


/*
* Initializes I2C, BME688 sensor and starts the BME task
*/
void bme688_device_create_task();
