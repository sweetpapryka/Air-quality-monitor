idf_component_register(SRCS "nvs_component.c"
                    INCLUDE_DIRS "include"
                    PRIV_REQUIRES "nvs_flash" "nvs_sec_provider"
                    )

