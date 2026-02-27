# Licensed under the Apache License, Version 2.0 (the "License");

cmake_minimum_required(VERSION 3.28)
message(STATUS "CMake version: ${CMAKE_VERSION}")

project(Xstd LANGUAGES CXX)

# ----- C++ standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ----- vcpkg integration
if(DEFINED CMAKE_TOOLCHAIN_FILE)
    message(STATUS "Using vcpkg toolchain: ${CMAKE_TOOLCHAIN_FILE}")
endif()

# ----- Packages
find_package(zstd   CONFIG REQUIRED)
find_package(GTest  CONFIG REQUIRED)
find_package(OpenSSL       REQUIRED)
find_package(fmt    CONFIG REQUIRED)


# ----- Compiler flags

set(DEBUG_O_LEVEL "g" CACHE STRING "The -Ox level used for debug builds")

set(CMAKE_CXX_FLAGS                     "${CMAKE_CXX_FLAGS} ${COMPILER_FLAGS} ${CMAKE_CXX_FLAGS_ADD}")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO      "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} -O3 ${DEBUG_INFO_FLAGS} ${CMAKE_CXX_FLAGS_ADD}")
set(CMAKE_CXX_FLAGS_DEBUG               "${CMAKE_CXX_FLAGS_DEBUG} -O${DEBUG_O_LEVEL} ${DEBUG_INFO_FLAGS} ${CMAKE_CXX_FLAGS_ADD}")

# ----- Project directories
set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(PACKAGE_DIR "${SRC_DIR}/packages")
set(TEST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tests")

add_library(Xstd
    ${SRC_DIR}/hasher.cc
    ${SRC_DIR}/page_manager.cc
    ${SRC_DIR}/compression.cc
    ${SRC_DIR}/encryption.cc
    ${SRC_DIR}/sha256.cc
    ${SRC_DIR}/catalog.cc
    ${SRC_DIR}/archive_writer.cc
    ${SRC_DIR}/archive_reader.cc
)

target_link_libraries(Xstd PUBLIC
    zstd::libzstd
    OpenSSL::SSL
    OpenSSL::Crypto
    fmt::fmt
)

target_include_directories(Xstd PUBLIC
    ${SRC_DIR}
    ${SRC_DIR}/packages
)

# ----- Add tests subdirectory
add_subdirectory(${TEST_DIR})
add_subdirectory(${PACKAGE_DIR}/gdelta)
add_subdirectory(${PACKAGE_DIR}/xxhash)

# ----- Additional Build Targets
# Add more targets here with zstd support:
# add_executable(my_app main.cpp)
# target_link_libraries(my_app PRIVATE Xstd zstd::libzstd)
#
# ----- Usage Notes
# zstd library is now integrated. To use it in your code:
# 1. Include header: #include <zstd.h>
# 2. Link library: target_link_libraries(your_target PRIVATE zstd::libzstd)
# 3. Use functions: ZSTD_compress(), ZSTD_decompress(), etc.