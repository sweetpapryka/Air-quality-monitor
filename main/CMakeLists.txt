# See the build system documentation in IDF programming guide
# for more information about component CMakeLists.txt files.

idf_component_register(
    SRCS main.c        
    INCLUDE_DIRS       
    PRIV_INCLUDE_DIRS  
    REQUIRES "esp_driver_gpio" "esp_partition" "nvs_flash" "esp_netif" "bme688" "pms5003" "bme688_device"
    PRIV_REQUIRES
    EMBED_TXTFILES "./certs/ca.pem"       
)
