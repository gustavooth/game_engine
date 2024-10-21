cmake_minimum_required(VERSION 3.30)

file(GLOB_RECURSE SOURCES "*.c")

include_directories(/usr/include ${CMAKE_SOURCE_DIR}/game_test/ ${CMAKE_SOURCE_DIR}/engine/)

add_executable(game_test ${SOURCES})

target_link_libraries(game_test engine)
