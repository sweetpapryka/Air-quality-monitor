#pragma once


#include <inttypes.h>


typedef struct pms_data
{
	uint16_t pm1_data;
	uint16_t pm2_5_data;
	uint16_t pm10_data;
}pms_data_t;


extern pms_data_t pms5003_data;


/*
* Initializes UART, PMS sensor and starts the PMS-UART-Queue task
*/
void start_pms5003();
