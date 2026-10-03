# Firmware of the standalone display for the WiCAN W906 fork (Elecrow CrowPanel 2.1 rotary, ESP32-S3).
# Own ESP-IDF project: idf.py -C display build
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(wican-display)
