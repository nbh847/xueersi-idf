option(XIAOMIAO_FRAMEWORK_SELF_TEST "Compile the App Framework runtime self test" OFF)
option(XIAOMIAO_NAVIGATION_SELF_TEST "Compile the Navigation runtime self test" OFF)
option(XIAOMIAO_LAUNCHER_SELF_TEST "Compile the Launcher runtime self test" OFF)
option(XIAOMIAO_SETTINGS_SERVICE_SELF_TEST "Compile the Settings Service self test" OFF)

set(FRAMEWORK_SRCS "framework/xiaomiao_app_registry.c"
                   "framework/xiaomiao_app_manager.c"
                   "framework/xiaomiao_navigation.c"
                   "framework/xiaomiao_launcher.c"
                   "framework/xiaomiao_wifi_indicator.c")

# System Services (goal nodes 9, 10 and 11). The Settings Service owns
# the persisted configuration, the Wi-Fi Service owns the whole network
# lifecycle including the provisioning page, and the Agent Service owns
# the HTTP link to the PC Agent; Apps only see their public interfaces.
set(SERVICE_SRCS "services/xiaomiao_settings_service.c"
                 "services/xiaomiao_wifi_service.c"
                 "services/xiaomiao_wifi_provisioning.c"
                 "services/xiaomiao_wifi_dns.c"
                 "services/xiaomiao_agent_service.c")

# Business Apps. The self test builds keep their own test data and never
# register these descriptions (goal node 5, decision 14).
set(APP_SRCS "apps/games/xiaomiao_games.c"
             "apps/pc_monitor/xiaomiao_pc_monitor.c"
             "apps/tools/xiaomiao_tools.c"
             "apps/settings/xiaomiao_settings.c")

if(XIAOMIAO_FRAMEWORK_SELF_TEST)
    list(APPEND FRAMEWORK_SRCS "framework/xiaomiao_framework_selftest.c")
endif()

if(XIAOMIAO_NAVIGATION_SELF_TEST)
    list(APPEND FRAMEWORK_SRCS "framework/xiaomiao_navigation_selftest.c")
endif()

if(XIAOMIAO_LAUNCHER_SELF_TEST)
    list(APPEND FRAMEWORK_SRCS "framework/xiaomiao_launcher_selftest.c")
endif()

if(XIAOMIAO_SETTINGS_SERVICE_SELF_TEST)
    list(APPEND SERVICE_SRCS "services/xiaomiao_settings_service_selftest.c")
endif()

idf_component_register(SRCS "main.c" ${FRAMEWORK_SRCS} ${SERVICE_SRCS} ${APP_SRCS}
                       PRIV_REQUIRES esp_lcd
                                     esp_timer
                                     esp_app_format
                                     esp_driver_gpio
                                     esp_driver_spi
                                     esp_driver_i2c
                                     esp_driver_ledc
                                     esp_driver_sdspi
                                     esp_adc
                                     esp_hw_support
                                     esp_wifi
                                     esp_netif
                                     esp_event
                                     esp_http_server
                                     esp_http_client
                                      cjson
                                     lwip
                                     heap
                                     spi_flash
                                     fatfs
                                     sdmmc
                                     nvs_flash
                                     lvgl
                       INCLUDE_DIRS ".")

if(XIAOMIAO_FRAMEWORK_SELF_TEST)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE XIAOMIAO_FRAMEWORK_SELF_TEST=1)
endif()

if(XIAOMIAO_NAVIGATION_SELF_TEST)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE XIAOMIAO_NAVIGATION_SELF_TEST=1)
endif()

if(XIAOMIAO_LAUNCHER_SELF_TEST)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE XIAOMIAO_LAUNCHER_SELF_TEST=1)
endif()

if(XIAOMIAO_SETTINGS_SERVICE_SELF_TEST)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE XIAOMIAO_SETTINGS_SERVICE_SELF_TEST=1)
endif()

target_link_libraries(${COMPONENT_LIB} PRIVATE m)
