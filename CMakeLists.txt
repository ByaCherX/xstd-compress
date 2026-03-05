# Licensed under the Apache License, Version 2.0 (the "License");

cmake_minimum_required(VERSION 3.28)
message(STATUS "CMake version: ${CMAKE_VERSION}")

# ----- XSTD GUI Application (optional)
option(XSTD_APP "Optional GUI application using Qt6" OFF)
if (XSTD_APP)
    list(APPEND VCPKG_MANIFEST_FEATURES "gui")
endif()
# ---------------------------------------------

project(Xstd LANGUAGES CXX)

# ----- C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_EXTENSIONS ON)
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
find_package(CLI11  CONFIG REQUIRED)


# ----- Compiler flags

set(DEBUG_O_LEVEL "g" CACHE STRING "The -Ox level used for debug builds")

set(CMAKE_CXX_FLAGS                     "${CMAKE_CXX_FLAGS} ${COMPILER_FLAGS} ${CMAKE_CXX_FLAGS_ADD}")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO      "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} -O3 ${DEBUG_INFO_FLAGS} ${CMAKE_CXX_FLAGS_ADD}")
set(CMAKE_CXX_FLAGS_DEBUG               "${CMAKE_CXX_FLAGS_DEBUG} -O${DEBUG_O_LEVEL} ${DEBUG_INFO_FLAGS} ${CMAKE_CXX_FLAGS_ADD}")

# ----- Project directories
set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(PACKAGE_DIR "${SRC_DIR}/packages")
set(TEST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tests")

set(XSTD_SOURCES
    ${SRC_DIR}/archive.cc
    ${SRC_DIR}/archive_reader.cc
    ${SRC_DIR}/archive_writer.cc
    ${SRC_DIR}/catalog.cc
    ${SRC_DIR}/compression.cc
    ${SRC_DIR}/encryption.cc
    ${SRC_DIR}/iohandler.cc
    ${SRC_DIR}/page_manager.cc
    ${SRC_DIR}/sha256.cc
)

# ----- Shared Library
add_library(Xstd SHARED ${XSTD_SOURCES})
# On Windows, automatically export all symbols from the shared library
# so that consumers do not need manual __declspec(dllexport) annotations.
set_target_properties(Xstd PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)

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

# ----- Static Library
add_library(Xstd_static STATIC ${XSTD_SOURCES})

target_link_libraries(Xstd_static PUBLIC
    zstd::libzstd
    OpenSSL::SSL
    OpenSSL::Crypto
    fmt::fmt
)
target_include_directories(Xstd_static PUBLIC
    ${SRC_DIR}
    ${SRC_DIR}/packages
)

# ----- Add tests subdirectory
option(XSTD_TESTS "Enable building tests" ON)
if (XSTD_TESTS)
    message(STATUS "XSTD Tests enabled")
    add_subdirectory(${TEST_DIR})
endif()

add_subdirectory(${PACKAGE_DIR}/xxhash)

# ---- gdelta compression support (optional)
option(XSTD_GDELTA "Include gdelta compression support" OFF)
if (XSTD_GDELTA)
    add_subdirectory(${PACKAGE_DIR}/gdelta)
endif()

# ----- CLI
option(XSTD_CLI "Include CLI application" ON)
if (XSTD_CLI)
    message(STATUS "XSTD CLI enabled")
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/cli)
endif()

# ------ GUI application (optional, requires Qt6)
if (XSTD_APP)
    message(STATUS "XSTD GUI application enabled")
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/app)
endif()