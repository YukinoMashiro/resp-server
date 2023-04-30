cmake_minimum_required(VERSION 3.16)
project(resp_server C)

set(CMAKE_C_STANDARD 99)

file(GLOB SRC src/*.c)

add_executable(resp-server ${SRC})

target_link_libraries(resp-server  m)
