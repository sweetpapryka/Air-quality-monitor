#pragma once

#include "freertos/FreeRTOS.h"


// Mutex to guard 1st and 2nd core accessing nvs at the same time
extern SemaphoreHandle_t nvs_cmpnt_semaphore;


void nvs_cmpnt_init(void);
