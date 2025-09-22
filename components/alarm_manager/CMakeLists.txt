
idf_component_register(SRCS "alarm_manager.c"
                       INCLUDE_DIRS "."
                       REQUIRES freertos nvs_flash storage_manager time_manager)
