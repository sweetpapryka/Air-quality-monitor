#include "freertos/FreeRTOS.h"

#include "ui.h"
#include "display.h"


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
	event_type_t event_type;
}event_t;

typedef struct page{
	void (*on_enter) (void);
	void (*on_event) (event_t event);
	void (*on_exit) (void);
}page_t;


QueueHandle_t event_queue;

pages_t current_page = PAGE_HOME;
pages_t next_page = PAGE_HOME;

// PAGE HOME
static void home_enter()
{
	display_draw_home();
}

static void home_event(event_t event)
{
	
}


void ui_task(void *arg)
{
	static page_t ui[PAGE_COUNT] = {
		[PAGE_HOME] = {home_enter, home_event},
		
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
	
	event_t event = {0};
	event_queue = xQueueCreate(QUEUE_SIZE, sizeof(event_t));
	
	ui[current_page].on_enter();
	for(;;)
	{
		if(xQueueReceive(event_queue, &event, pdMS_TO_TICKS(1000)) == pdFAIL)
		{
			event.event_type = EVENT_TICK;
		}
		
		if(ui[current_page].on_event)
			ui[current_page].on_event(event);
		
		if(next_page != current_page)
		{
			if(ui[current_page].on_exit)
				ui[current_page].on_exit();
			ui[next_page].on_enter();
			current_page = next_page;	
		}
	}
}

void ui_create_task()
{
	xTaskCreatePinnedToCore(ui_task, "ui_task", 4096, NULL, 5, NULL, PRO_CPU_NUM);
}


































