idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES alarm_manager
                                button_manager
                                neopixel_animations
                                neopixel_driver
                                pot_manager
                                storage_manager
                                time_manager
                                wifi_manager
                                spiffs)

set(WEB_DIST_DIR "${CMAKE_CURRENT_LIST_DIR}/../web_frontend/dist")
set(EMPTY_SPIFFS_DIR "${CMAKE_CURRENT_BINARY_DIR}/empty_spiffs")

if(EXISTS "${WEB_DIST_DIR}")
    spiffs_create_partition_image(spiffs "${WEB_DIST_DIR}" FLASH_IN_PROJECT)
else()
    message(WARNING "web_frontend/dist not found. Build the frontend before flashing for embedded UI.")
    file(MAKE_DIRECTORY "${EMPTY_SPIFFS_DIR}")
    spiffs_create_partition_image(spiffs "${EMPTY_SPIFFS_DIR}" FLASH_IN_PROJECT)
endif()
