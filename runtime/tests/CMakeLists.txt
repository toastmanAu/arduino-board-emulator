include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
)
# Prevent gtest from being installed by the host project
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(runtime_tests
    test_smoke.cpp
    test_timing.cpp
    test_pin_state.cpp
    test_string.cpp
    test_serial.cpp
    test_bus_logging.cpp
    test_event_pump.cpp
    test_gpio_log.cpp
    test_displays_ili9488.cpp
    test_displays_ssd1306.cpp
    test_displays_extra.cpp
    test_lvgl_init.cpp
    test_screenshot.cpp
    test_touch_sdl.cpp
    test_net_mode.cpp
    test_wifi.cpp
    test_http.cpp
    test_eeprom.cpp
    test_fs.cpp
    test_webserver.cpp
    test_wifi_secure.cpp
    test_websockets.cpp
    test_mdns.cpp
    test_update.cpp
    test_mqtt.cpp
)
target_link_libraries(runtime_tests PRIVATE sim_runtime GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(runtime_tests)
