/*
 * Air-quality-monitor: buttons + BME688 (Bosch bme68x driver, own I2C glue)
 * ESP32-S3, ESP-IDF v5.3.x
 *
 * Requires bme68x.c, bme68x.h, bme68x_defs.h from
 * https://github.com/boschsensortec/BME68x-Sensor-API
 * placed next to this file in main/.
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_rom_sys.h"

#include "bme68x.h"

/* ---------------- Buttons (active LOW, internal pull-up) ---------------- */
#define GPIO_UP      GPIO_NUM_4
#define GPIO_RIGHT   GPIO_NUM_5
#define GPIO_DOWN    GPIO_NUM_6
#define GPIO_LEFT    GPIO_NUM_7
#define GPIO_ENTER   GPIO_NUM_9

#define DEBOUNCE_MS  200

#define BUTTON_PIN_SEL ( (1ULL << GPIO_UP)    | \
                         (1ULL << GPIO_RIGHT) | \
                         (1ULL << GPIO_DOWN)  | \
                         (1ULL << GPIO_LEFT)  | \
                         (1ULL << GPIO_ENTER) )

/* ---------------- I2C ---------------- */
/* !!! SET THESE TO THE PINS YOUR SENSOR WIRES ARE ACTUALLY ON !!!
 * (Avoid 4,5,6,7,9 which the buttons use.) */
#define I2C_PORT      I2C_NUM_0
#define I2C_SDA_GPIO  2
#define I2C_SCL_GPIO  11
#define I2C_SPEED_HZ  100000

static const char *TAG = "aqm";

/* ================= Buttons ================= */

static QueueHandle_t gpio_evt_queue = NULL;

/* Per-pin debounce timestamps (microseconds), indexed by GPIO number */
static volatile int64_t last_isr_time[GPIO_NUM_MAX];

static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    uint32_t gpio_num = (uint32_t)arg;
    int64_t now = esp_timer_get_time();

    if ((now - last_isr_time[gpio_num]) < ((int64_t)DEBOUNCE_MS * 1000)) {
        return;
    }
    last_isr_time[gpio_num] = now;

    xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL);
}

static const char *button_name(uint32_t gpio_num)
{
    switch (gpio_num) {
        case GPIO_UP:    return "UP";
        case GPIO_RIGHT: return "RIGHT";
        case GPIO_DOWN:  return "DOWN";
        case GPIO_LEFT:  return "LEFT";
        case GPIO_ENTER: return "ENTER";
        default:         return "UNKNOWN";
    }
}

static void button_task(void *arg)
{
    uint32_t gpio_num;
    for (;;) {
        if (xQueueReceive(gpio_evt_queue, &gpio_num, portMAX_DELAY)) {
            int64_t ms = esp_timer_get_time() / 1000;
            ESP_LOGI(TAG, "Button pressed: %-6s (GPIO%-2ld) at %lld ms",
                     button_name(gpio_num), (long)gpio_num, ms);
        }
    }
}

static esp_err_t buttons_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = BUTTON_PIN_SEL,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,       /* buttons go to GND */
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));
    if (gpio_evt_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create queue");
        return ESP_ERR_NO_MEM;
    }

    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_isr_handler_add(GPIO_UP,    gpio_isr_handler, (void *)GPIO_UP));
    ESP_ERROR_CHECK(gpio_isr_handler_add(GPIO_RIGHT, gpio_isr_handler, (void *)GPIO_RIGHT));
    ESP_ERROR_CHECK(gpio_isr_handler_add(GPIO_DOWN,  gpio_isr_handler, (void *)GPIO_DOWN));
    ESP_ERROR_CHECK(gpio_isr_handler_add(GPIO_LEFT,  gpio_isr_handler, (void *)GPIO_LEFT));
    ESP_ERROR_CHECK(gpio_isr_handler_add(GPIO_ENTER, gpio_isr_handler, (void *)GPIO_ENTER));

    xTaskCreate(button_task, "button_task", 4096, NULL, 10, NULL);
    return ESP_OK;
}

/* ================= BME688 (Bosch bme68x driver) ================= */

static i2c_master_bus_handle_t bus_handle;
static i2c_master_dev_handle_t bme_dev;

static struct bme68x_dev  bme;
static struct bme68x_conf bme_conf;
static struct bme68x_heatr_conf bme_heatr;

/* Bosch callbacks: return 0 on success, non-zero on failure */
static int8_t bme_read(uint8_t reg, uint8_t *data, uint32_t len, void *intf)
{
    esp_err_t e = i2c_master_transmit_receive(bme_dev, &reg, 1, data, len, 100);
    return (e == ESP_OK) ? 0 : -1;
}

static int8_t bme_write(uint8_t reg, const uint8_t *data, uint32_t len, void *intf)
{
    uint8_t buf[32];
    if (len > sizeof(buf) - 1) {
        return -1;
    }
    buf[0] = reg;
    memcpy(&buf[1], data, len);
    esp_err_t e = i2c_master_transmit(bme_dev, buf, len + 1, 100);
    return (e == ESP_OK) ? 0 : -1;
}

static void bme_delay_us(uint32_t us, void *intf)
{
    if (us >= 1000) {
        TickType_t ticks = pdMS_TO_TICKS((us + 999) / 1000);
        vTaskDelay(ticks ? ticks : 1);
    } else {
        esp_rom_delay_us(us);
    }
}

static esp_err_t i2c_bus_init(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_PORT,
        .sda_io_num = I2C_SDA_GPIO,
        .scl_io_num = I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = false,   /* external pull-ups fitted */
    };
    return i2c_new_master_bus(&bus_cfg, &bus_handle);
}

/* Try 0x76 then 0x77 and return whichever answers, or 0 if none */
static uint8_t find_bme_address(void)
{
    const uint8_t candidates[] = { 0x76, 0x77 };
    for (size_t i = 0; i < sizeof(candidates); i++) {
        if (i2c_master_probe(bus_handle, candidates[i], 100) == ESP_OK) {
            ESP_LOGI(TAG, "Device ACKs at 0x%02X", candidates[i]);
            return candidates[i];
        }
    }
    return 0;
}

static esp_err_t bme_setup(void)
{
    uint8_t addr = find_bme_address();
    if (addr == 0) {
        ESP_LOGE(TAG, "No device at 0x76 or 0x77 (check wiring/power)");
        return ESP_ERR_NOT_FOUND;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = I2C_SPEED_HZ,
    };
    esp_err_t err = i2c_master_bus_add_device(bus_handle, &dev_cfg, &bme_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "add_device failed: %s", esp_err_to_name(err));
        return err;
    }

    /* Raw chip-ID read, independent of the Bosch driver (expect 0x61) */
    uint8_t reg = 0xD0, id = 0;
    err = i2c_master_transmit_receive(bme_dev, &reg, 1, &id, 1, 100);
    ESP_LOGI(TAG, "Raw chip ID read: %s, id=0x%02X (expect 0x61)",
             esp_err_to_name(err), id);
    if (err != ESP_OK) {
        return err;
    }

    memset(&bme, 0, sizeof(bme));
    bme.intf     = BME68X_I2C_INTF;
    bme.read     = bme_read;
    bme.write    = bme_write;
    bme.delay_us = bme_delay_us;
    bme.intf_ptr = NULL;
    bme.amb_temp = 25;

    int8_t rc = bme68x_init(&bme);
    ESP_LOGI(TAG, "bme68x_init = %d (0 ok, -2 comm fail, -3 wrong chip id)", rc);
    if (rc != BME68X_OK) {
        return ESP_FAIL;
    }

    bme_conf.os_hum  = BME68X_OS_2X;
    bme_conf.os_pres = BME68X_OS_4X;
    bme_conf.os_temp = BME68X_OS_8X;
    bme_conf.filter  = BME68X_FILTER_OFF;
    bme_conf.odr     = BME68X_ODR_NONE;
    rc = bme68x_set_conf(&bme_conf, &bme);
    if (rc != BME68X_OK) {
        ESP_LOGE(TAG, "bme68x_set_conf = %d", rc);
        return ESP_FAIL;
    }

    bme_heatr.enable     = BME68X_ENABLE;
    bme_heatr.heatr_temp = 300;   /* degC */
    bme_heatr.heatr_dur  = 100;   /* ms */
    rc = bme68x_set_heatr_conf(BME68X_FORCED_MODE, &bme_heatr, &bme);
    if (rc != BME68X_OK) {
        ESP_LOGE(TAG, "bme68x_set_heatr_conf = %d", rc);
        return ESP_FAIL;
    }

    return ESP_OK;
}

static void bme688_task(void *arg)
{
    for (;;) {
        int8_t rc = bme68x_set_op_mode(BME68X_FORCED_MODE, &bme);
        if (rc != BME68X_OK) {
            ESP_LOGE(TAG, "set_op_mode = %d", rc);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        uint32_t wait_us = bme68x_get_meas_dur(BME68X_FORCED_MODE, &bme_conf, &bme)
                           + (uint32_t)bme_heatr.heatr_dur * 1000;
        bme.delay_us(wait_us, bme.intf_ptr);

        struct bme68x_data d;
        uint8_t n = 0;
        rc = bme68x_get_data(BME68X_FORCED_MODE, &d, &n, &bme);
        if (rc == BME68X_OK && n > 0) {
#ifdef BME68X_USE_FPU
            ESP_LOGI(TAG, "T=%.2f degC  P=%.2f Pa  RH=%.2f %%  gas=%.0f ohm (gas_valid=%d, heater_stable=%d)",
                     d.temperature, d.pressure, d.humidity, d.gas_resistance,
                     !!(d.status & BME68X_GASM_VALID_MSK),
                     !!(d.status & BME68X_HEAT_STAB_MSK));
#else
            /* Integer build: T in 0.01 degC, P in Pa, RH in 0.001 %, gas in ohm */
            ESP_LOGI(TAG, "T=%.2f degC  P=%lu Pa  RH=%.2f %%  gas=%lu ohm (gas_valid=%d, heater_stable=%d)",
                     d.temperature / 100.0f, (unsigned long)d.pressure,
                     d.humidity / 1000.0f, (unsigned long)d.gas_resistance,
                     !!(d.status & BME68X_GASM_VALID_MSK),
                     !!(d.status & BME68X_HEAT_STAB_MSK));
#endif
        } else {
            ESP_LOGW(TAG, "get_data rc=%d, new fields=%u", rc, n);
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/* ================= app_main ================= */

void app_main(void)
{
    if (buttons_init() != ESP_OK) {
        return;
    }

    esp_err_t err = i2c_bus_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_master_bus failed: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "I2C idle levels: SDA=%d SCL=%d (both should be 1)",
             gpio_get_level(I2C_SDA_GPIO), gpio_get_level(I2C_SCL_GPIO));

    if (bme_setup() == ESP_OK) {
        xTaskCreate(bme688_task, "bme688_task", 8192, NULL, 5, NULL);
    } else {
        ESP_LOGE(TAG, "BME688 setup failed; buttons still active");
    }

    ESP_LOGI(TAG, "Ready. Press UP/RIGHT/DOWN/LEFT/ENTER...");
}