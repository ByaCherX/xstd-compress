cmake_minimum_required(VERSION 3.10)
project(gdelta LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Set default build type to Release if not specified
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Platform-specific compiler flags
if(MSVC)
    # MSVC compiler flags
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4 /permissive-")
    set(CMAKE_CXX_FLAGS_DEBUG "/Zi /Od /D_DEBUG")
    set(CMAKE_CXX_FLAGS_RELEASE "/O2 /DNDEBUG")
else()
    # GCC/Clang compiler flags
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")
    set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -D_DEBUG")
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
endif()

# Create static library
add_library(gdelta STATIC
    gdelta.cpp
)

target_include_directories(gdelta PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Create executable
add_executable(gdelta_main gdelta_main.cpp)

target_link_libraries(gdelta_main PRIVATE gdelta)

target_include_directories(gdelta_main PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)