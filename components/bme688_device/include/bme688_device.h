#pragma once


#include <inttypes.h>

#include "bme68x.h"


extern struct bme68x_data bme688_data;


/*
* Initializes I2C, BME688 sensor and starts the BME task
*/
void start_bme688();
