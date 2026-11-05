cmake_minimum_required(VERSION 3.16)
project(gps_receiver_simulator VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(GPS_WERROR "Treat warnings as errors" ON)
option(GPS_SANITIZE "Build with AddressSanitizer + UBSan" OFF)

add_library(gpscore STATIC
  src/gps_receiver.c src/nmea_parser.c src/nmea_checksum.c src/coordinate_converter.c)
target_include_directories(gpscore PUBLIC include)
target_compile_options(gpscore PUBLIC -Wall -Wextra -Wpedantic -Wshadow
  -Wstrict-prototypes -Wformat=2 -Wconversion -Wno-sign-conversion)
if(GPS_WERROR)
  target_compile_options(gpscore PUBLIC -Werror)
endif()
if(GPS_SANITIZE)
  target_compile_options(gpscore PUBLIC -fsanitize=address,undefined
    -fno-sanitize-recover=all -fno-omit-frame-pointer)
  target_link_options(gpscore PUBLIC -fsanitize=address,undefined)
endif()

add_executable(gps_receiver src/main.c)
target_link_libraries(gps_receiver PRIVATE gpscore)

enable_testing()
foreach(t test_checksum test_parser test_coordinates)
  add_executable(${t} tests/c/${t}.c)
  target_include_directories(${t} PRIVATE tests/c)
  target_link_libraries(${t} PRIVATE gpscore)
  add_test(NAME ${t} COMMAND ${t})
endforeach()

find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  add_test(NAME python_unit
    COMMAND ${Python3_EXECUTABLE} -m unittest discover -s tests/python
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  add_test(NAME integration
    COMMAND ${CMAKE_COMMAND} -E env GPS_RECEIVER_BIN=$<TARGET_FILE:gps_receiver>
            ${Python3_EXECUTABLE} -m unittest discover -s tests/integration
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endif()
