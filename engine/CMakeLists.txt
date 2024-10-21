cmake_minimum_required(VERSION 3.30)

add_definitions(-DEGNEXPORT)

file(GLOB_RECURSE SOURCES "*.c")

include_directories(/usr/include ${CMAKE_SOURCE_DIR}/engine/)

add_library(engine SHARED ${SOURCES})

target_link_libraries(engine wayland-client vulkan)
