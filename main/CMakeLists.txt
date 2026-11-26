# See the build system documentation in IDF programming guide
# for more information about component CMakeLists.txt files.

idf_component_register(
    SRCS main.c        
    INCLUDE_DIRS       
    PRIV_INCLUDE_DIRS  
    REQUIRES "esp_driver_gpio" "esp_partition" "bme688" "pms5003" "bme688_device" "nvs_component" "display" "ui"
    PRIV_REQUIRES
    EMBED_TXTFILES "./certs/ca.pem"       
)
