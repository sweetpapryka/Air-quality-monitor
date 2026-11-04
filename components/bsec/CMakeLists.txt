idf_component_register(                    INCLUDE_DIRS "include")

add_prebuilt_library(algobsec "${CMAKE_CURRENT_SOURCE_DIR}/lib/libalgobsec.a")
target_link_libraries(${COMPONENT_LIB} INTERFACE algobsec)