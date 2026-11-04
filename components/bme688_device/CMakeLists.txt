idf_component_register(SRCS "bme688_device.c"
                    INCLUDE_DIRS "include"
                    REQUIRES "bme688"
                    PRIV_REQUIRES "esp_driver_i2c")

