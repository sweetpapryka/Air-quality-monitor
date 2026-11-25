#include "freertos/FreeRTOS.h"

#include "UI.h"
#include "display.h"
#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"


#define QUEUE_SIZE					2


typedef enum event_type{
	EVENT_TICK,
 	EVENT_BUTTON_ENTER,
	EVENT_BUTTON_UP,
	EVENT_BUTTON_RIGHT,
	EVENT_BUTTON_DOWN,
	EVENT_BUTTON_LEFT
}event_type_t;

// Every underscore represents the level of nesting 
typedef enum pages{
	// Main screen
	PAGE_HOME,
	// Settings
	PAGE_SETTINGS,
	// In settings, displays unpair if WiFi is connected, pair if no connection is established
	PAGE_SETTINGS_PAIR,
	PAGE_SETTINGS_PAIR_CONNECTING,				
	PAGE_SETTINGS_PAIR_CONNECTED,			
	PAGE_SETTINGS_UNPAIR,
	PAGE_SETTINGS_UNPAIR_CONFIRM,
	// MQTT pages for creating and deleting credentials
	PAGE_SETTINGS_MQTTCREATE,
	PAGE_SETTINGS_MQTTCREATE_COMPLETED,
	PAGE_SETTINGS_MQTTDELETE,
	PAGE_SETTINGS_MQTTDELETE_CONFIRM,
	// Page for resetting the built-up BSEC state
	PAGE_SETTINGS_IAQRESET,
	PAGE_SETTINGS_IAQRESET_CONFIRM,
	// Page where the user can enable child-lock
	PAGE_SETTINGS_CHILDLOCK,
	PAGE_SETTINGS_CHILDLOCK_CONFIRM,
	
	PAGE_COUNT
}pages_t;


typedef struct event{
	event_type_t event
}event_t;

typedef struct page{
	void (*on_enter) (void);
	void (*on_event) (void);
	void (*on_exit) (void);
}page_t;


QueueHandle_t event_queue;


void ui_task(void *arg)
{
	static page_t ui[PAGE_COUNT] = {
		[PAGE_HOME] = {},
		
		[PAGE_SETTINGS] = {},
		
		[PAGE_SETTINGS_PAIR] = {},
		[PAGE_SETTINGS_PAIR_CONNECTING] = {},				
		[PAGE_SETTINGS_PAIR_CONNECTED] = {},			
		[PAGE_SETTINGS_UNPAIR] = {},
		[PAGE_SETTINGS_UNPAIR_CONFIRM] = {},
		
		[PAGE_SETTINGS_MQTTCREATE] = {},
		[PAGE_SETTINGS_MQTTCREATE_COMPLETED] = {},
		[PAGE_SETTINGS_MQTTDELETE] = {},
		[PAGE_SETTINGS_MQTTDELETE_CONFIRM] = {},
		
		[PAGE_SETTINGS_IAQRESET] = {},
		[PAGE_SETTINGS_IAQRESET_CONFIRM] = {},
		
		[PAGE_SETTINGS_CHILDLOCK] = {},
		[PAGE_SETTINGS_CHILDLOCK_CONFIRM] = {},
	};
	
	event_queue = xQueueCreate(QUEUE_SIZE, sizeof(event_t));
	
	for(;;)
	{
		event_t queue_buffer = {0};
		if(xQueueReceive(event_queue, &queue_buffer, pdMS_TO_TICKS(1000)) == pdFAIL)
		{
			queue_buffer.event = EVENT_TICK;
		}
		
		
	}
}

void ui_task_create()
{
	xTaskCreatePinnedToCore(ui_task, "ui_task", 4096, NULL, 5, NULL, PRO_CPU_NUM);
}


































