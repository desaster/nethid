cmake_minimum_required(VERSION 3.17)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

set(TARGET nethid)

set(WIFI_SSID "$ENV{WIFI_SSID}")
set(WIFI_PASSWORD "$ENV{WIFI_PASSWORD}")

if(NOT WIFI_SSID)
  message(FATAL_ERROR "WIFI_SSID not set")
endif()

if(NOT WIFI_PASSWORD)
  message(FATAL_ERROR "WIFI_PASSWORD not set")
endif()

set(PICO_BOARD pico_w CACHE STRING "Board type")

# Pull in the Pico SDK.  This correctly pulls in TinyUSB for us.
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

if (PICO_SDK_VERSION_STRING VERSION_LESS "1.4.0")
  message(FATAL_ERROR "Raspberry Pi Pico SDK version 1.4.0 (or later) required. Your version is ${PICO_SDK_VERSION_STRING}")
endif()

project(${TARGET})
pico_sdk_init()

add_executable(${TARGET}
    src/main.c
    src/board.c
    src/usb.c
    src/usb_descriptors.c
    src/httpd/httpd.c
    #${PICO_SDK_PATH}/lib/lwip/src/apps/mqtt/mqtt.c
    )

target_link_libraries(${TARGET}
    pico_cyw43_arch_lwip_poll
    pico_lwip_http
    pico_stdlib
    pico_multicore
    #pico_lwip_mqtt
    tinyusb_board
    tinyusb_device
    )

pico_add_extra_outputs(${TARGET})


target_compile_definitions(${TARGET} PRIVATE
    WIFI_SSID=\"${WIFI_SSID}\"
    WIFI_PASSWORD=\"${WIFI_PASSWORD}\"
    )

add_compile_options(-Wall
    -Wno-unused-function
    )

target_include_directories(${TARGET} PRIVATE
    $ENV{PICO_SDK_PATH}/lib/mbedtls/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
