# See the build system documentation in IDF programming guide
# for more information about component CMakeLists.txt files.

idf_component_register(
    SRCS main.c        
    INCLUDE_DIRS       
    PRIV_INCLUDE_DIRS  
    REQUIRES esp_driver_gpio bme688
    PRIV_REQUIRES       
)
