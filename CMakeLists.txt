# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

set(WICAN_V210 1)
set(WICAN_V300 2)
set(WICAN_USB_V100 3)
set(WICAN_PRO 4)
set(WICAN_DIY_ESP32 5)

set(WICAN_V210_STR "OBD")
set(WICAN_V300_STR "OBD")
set(WICAN_USB_V100_STR "USB")
set(WICAN_PRO_STR "OBD-PRO")
set(WICAN_DIY_ESP32_STR "OBD-DIY")

# Set these without quotes, as they're string literals
set(WICAN_V210_PREF "obd")
set(WICAN_V300_PREF "obd")
set(WICAN_USB_V100_PREF "usb")
set(WICAN_PRO_PREF "obd_pro")
set(WICAN_DIY_ESP32_PREF "obd_diy")

add_definitions(-DWICAN_V210=${WICAN_V210})
add_definitions(-DWICAN_V300=${WICAN_V300})
add_definitions(-DWICAN_USB_V100=${WICAN_USB_V100})
add_definitions(-DWICAN_PRO=${WICAN_PRO})
add_definitions(-DWICAN_DIY_ESP32=${WICAN_DIY_ESP32})

add_definitions(-DWICAN_V210_STR="${WICAN_V210_STR}")
add_definitions(-DWICAN_V300_STR="${WICAN_V300_STR}")
add_definitions(-DWICAN_USB_V100_STR="${WICAN_USB_V100_STR}")
add_definitions(-DWICAN_PRO_STR="${WICAN_PRO_STR}")
add_definitions(-DWICAN_DIY_ESP32_STR="${WICAN_DIY_ESP32_STR}")

execute_process(
    COMMAND git describe --tags --always --dirty
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_SHA
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

set(GIT_SHA1 "${GIT_SHA}")
string(REPLACE "." "" GIT_SHA "${GIT_SHA}")

add_definitions(-DGIT_SHA="${GIT_SHA1}")

# Uncomment and use one of the following as needed
# set(HARDWARE_VER ${WICAN_V210})
# set(HARDWARE_VER ${WICAN_USB_V100})
# set(HARDWARE_VER ${WICAN_PRO})
# Self-built board (ESP32-WROOM + CAN transceiver), see DIY_ESP32.md:
#   sh tools/diy_esp32/build.sh
if(WICAN_HW STREQUAL "DIY_ESP32")
    if(NOT IDF_TARGET STREQUAL "esp32")
        message(FATAL_ERROR "WICAN_HW=DIY_ESP32 needs IDF_TARGET=esp32 (is ${IDF_TARGET})")
    endif()
    set(HARDWARE_VER ${WICAN_DIY_ESP32})
else()
    set(HARDWARE_VER ${WICAN_V300})
endif()

add_definitions(-DHARDWARE_VER=${HARDWARE_VER})

set(PROJECT_PREF "wican-fw")

# Set HW_PREF based on HARDWARE_VER
if (HARDWARE_VER EQUAL ${WICAN_DIY_ESP32})
    set(HW_PREF "${WICAN_DIY_ESP32_PREF}")
    set(HW_VER "${WICAN_DIY_ESP32_STR}")
elseif (HARDWARE_VER EQUAL ${WICAN_PRO})
    set(HW_PREF "${WICAN_PRO_PREF}")
    set(HW_VER "${WICAN_PRO_STR}")
elseif (HARDWARE_VER EQUAL ${WICAN_USB_V100})
    set(HW_PREF "${WICAN_USB_V100_PREF}")
    set(HW_VER "${WICAN_USB_V100_STR}")
elseif (HARDWARE_VER EQUAL ${WICAN_V210})
    set(HW_PREF "${WICAN_V210_PREF}")
    set(HW_VER "${WICAN_V210_STR}")
else()
    set(HW_PREF "${WICAN_V300_PREF}")
    set(HW_VER "${WICAN_V300_STR}")
endif()

add_definitions(-DHARDWARE_VERSION="${HW_VER}")

# Form the project binary name
set(PRJ_BIN "${PROJECT_PREF}_${HW_PREF}_${GIT_SHA}")
project("${PRJ_BIN}")
