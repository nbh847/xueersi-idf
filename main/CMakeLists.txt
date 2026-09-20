option(XIAOMIAO_FRAMEWORK_SELF_TEST "Compile the App Framework runtime self test" OFF)

set(FRAMEWORK_SRCS "framework/xiaomiao_app_registry.c"
                   "framework/xiaomiao_app_manager.c")

if(XIAOMIAO_FRAMEWORK_SELF_TEST)
    list(APPEND FRAMEWORK_SRCS "framework/xiaomiao_framework_selftest.c")
endif()

idf_component_register(SRCS "main.c" ${FRAMEWORK_SRCS}
                       PRIV_REQUIRES esp_lcd
                                     esp_timer
                                     esp_driver_gpio
                                     esp_driver_spi
                                     esp_driver_i2c
                                     esp_driver_ledc
                                     esp_driver_sdspi
                                     esp_adc
                                     esp_hw_support
                                     heap
                                     spi_flash
                                     fatfs
                                     sdmmc
                                     lvgl
                       INCLUDE_DIRS "")

if(XIAOMIAO_FRAMEWORK_SELF_TEST)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE XIAOMIAO_FRAMEWORK_SELF_TEST=1)
endif()

target_link_libraries(${COMPONENT_LIB} PRIVATE m)
