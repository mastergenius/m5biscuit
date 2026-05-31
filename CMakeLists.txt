cmake_minimum_required(VERSION 3.16.0)

set(ENV{IDF_COMPONENT_MANAGER} "0")
set(SDKCONFIG_DEFAULTS "${CMAKE_SOURCE_DIR}/src/probes/sdkconfig.defaults")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(biscuit-keyboard-probe)
