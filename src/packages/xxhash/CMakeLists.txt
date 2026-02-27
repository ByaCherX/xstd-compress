cmake_minimum_required(VERSION 3.10)
project(xxhash LANGUAGES C)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED OFF)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(xxhash STATIC
    xxhash.c
)

target_include_directories(xxhash PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)