# Find Qt6 package from vcpkg (optional - app won't be built if Qt6 is not found)
# Qt6 must be installed via: vcpkg install qtbase:x64-windows
find_package(Qt6 COMPONENTS Widgets CONFIG QUIET)

if(NOT Qt6_FOUND)
    message(STATUS "Qt6 not found — GUI application will not be built.")
    message(STATUS "To enable the GUI application, install Qt6 via vcpkg:")
    message(STATUS "  vcpkg install qtbase:x64-windows")
    return()
endif()

message(STATUS "Qt6 found — building GUI application")

set(APP_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

# Enable Qt MOC/UIC/RCC automatically
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

set(APP_SOURCES
    ${APP_SRC_DIR}/main.cc
    ${APP_SRC_DIR}/mainwindow.h
    ${APP_SRC_DIR}/mainwindow.cc
    ${APP_SRC_DIR}/welcomewidget.h
    ${APP_SRC_DIR}/welcomewidget.cc
    ${APP_SRC_DIR}/archivemodel.h
    ${APP_SRC_DIR}/archivemodel.cc
)

qt_add_executable(xstd-app ${APP_SOURCES})

target_link_libraries(xstd-app PRIVATE
    Xstd
    Qt6::Widgets
    fmt::fmt
)

target_include_directories(xstd-app PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Suppress the console window on Windows (WIN32_EXECUTABLE)
set_target_properties(xstd-app PROPERTIES
    WIN32_EXECUTABLE ON
)

# Copy Xstd.dll next to xstd-app.exe
add_custom_command(TARGET xstd-app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:Xstd>
        $<TARGET_FILE_DIR:xstd-app>
    COMMENT "Copying Xstd shared library next to xstd-app"
)

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    find_program(TOOL_WINDEPLOYQT NAMES windeployqt.debug.bat)
else()
    find_program(TOOL_WINDEPLOYQT NAMES windeployqt.bat)
endif()

add_custom_command(TARGET xstd-app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "Running windeployqt to deploy Qt plugins..."
    COMMAND ${TOOL_WINDEPLOYQT}
        --no-translations
        --no-opengl-sw
        --no-system-d3d-compiler
        --no-compiler-runtime
        $<TARGET_FILE:xstd-app>
    COMMENT "Running windeployqt to deploy Qt plugins..."
)
