# The following lines of boilerplate have to be in your project's CMakeLists
# in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp-windsensor-mast)

set(CONFIG_LWIP_MAX_SOCKETS 16 CACHE STRING "LWIP parameter at maximum")