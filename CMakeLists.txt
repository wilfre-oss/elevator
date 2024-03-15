cmake_minimum_required(VERSION 3.15)
project(elevator C)

set(CMAKE_C_STANDARD 11)

add_executable(controller controller.c hardwareAPI.c)

include_directories(${CMAKE_SOURCE_DIR})

target_link_libraries(controller ws2_32)

find_package(Threads REQUIRED)
target_link_libraries(controller Threads::Threads)