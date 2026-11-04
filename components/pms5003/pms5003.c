#include <stdio.h>

#include <driver/uart.h>
#include "pms.h"
#include "pms5003.h"


#define UART_TX_PIN				17
#define UART_RX_PIN				18		


static const char TAG[] = "pms5003 component";

static QueueHandle_t data_queue;

pms_data_t pms5003_data;


static void pms_queue_task(void *pvParameters)
{
	uint8_t data[128];
	int data_len = 0;
	uart_event_t event;
	
   	for (;;)
   	{
		if (xQueueReceive(data_queue, (void *)&event, 100 / portTICK_PERIOD_MS))
		{
			switch(event.type)
			{
				case UART_DATA:
				if(pms_get_state() == PMS_STATE_ACTIVE){
					data_len = uart_read_bytes(pms_get_uart_port(), (uint8_t*)&data, sizeof(data), 0);
					if(data_len > 0)
					{
                        if(pms_parse_data((uint8_t*)&data, data_len) == ESP_OK)
						{
                            pms5003_data.pm1_data = pms_get_data(PMS_FIELD_PM1_ATM);
                            pms5003_data.pm2_5_data = pms_get_data(PMS_FIELD_PM2_5_ATM);
                            pms5003_data.pm10_data = pms_get_data(PMS_FIELD_PM10_ATM);
                        }
                    }
				}
				else
				{
					ESP_LOGW(TAG, "Data received while sensor not in active state, ignoring");
				}
				break;
				
				case UART_FIFO_OVF:
                ESP_LOGW(TAG, "HW FIFO overflow");
                uart_flush_input(pms_get_uart_port());
                xQueueReset(data_queue);
                break;

	            case UART_BUFFER_FULL:
	                ESP_LOGW(TAG, "Ring buffer full");
	                uart_flush_input(pms_get_uart_port());
	                xQueueReset(data_queue);
	                break;
	
	            default:
	                ESP_LOGI(TAG, "UART event type: %d", event.type);
	                break;
			}
		}
	}
}


void start_pms5003()
{
	// Init UART
	uart_config_t uart_config = 
	{
		.baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .stop_bits = UART_STOP_BITS_1,
        .parity = UART_PARITY_DISABLE,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
	};
	

	ESP_ERROR_CHECK(uart_set_pin(UART_NUM_1, GPIO_NUM_17, GPIO_NUM_18,
	                             UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
	ESP_ERROR_CHECK(uart_driver_install(UART_NUM_1, 1024 * 2, 0, 10, &data_queue, 0));
	ESP_ERROR_CHECK(uart_param_config(UART_NUM_1, &uart_config));
	
	// Init PMS
	pms_config_t pms_config = 
	{
		.type = PMS_TYPE_5003,
		.set_gpio = GPIO_NUM_9,
		.reset_gpio = GPIO_NUM_10,
		.uart_port = UART_NUM_1,
	};
	
	ESP_ERROR_CHECK(pms_init(&pms_config));
	xTaskCreate(pms_queue_task, "pms queue task", 4096, NULL, 10, NULL);
}
