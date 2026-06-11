set(CLI_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

set(CLI_SOURCES
    ${CLI_SRC_DIR}/main.cc
    ${CLI_SRC_DIR}/util.cc
    ${CLI_SRC_DIR}/commands/create.cc
    ${CLI_SRC_DIR}/commands/add.cc
    ${CLI_SRC_DIR}/commands/extract.cc
    ${CLI_SRC_DIR}/commands/list.cc
    ${CLI_SRC_DIR}/commands/delete.cc
    ${CLI_SRC_DIR}/commands/recover.cc
    ${CLI_SRC_DIR}/commands/info.cc
    ${CLI_SRC_DIR}/commands/compress.cc
)

add_executable(xli ${CLI_SOURCES})

target_link_libraries(xli PRIVATE
    Xstd
    OpenSSL::Crypto
    zstd::libzstd
    CLI11::CLI11
    fmt::fmt
)

target_include_directories(xli PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Copy Xstd.dll next to xli.exe on every build so the executable can find it
# at runtime without requiring the DLL to be on PATH.
add_custom_command(TARGET xli POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:Xstd>
        $<TARGET_FILE_DIR:xli>
    COMMENT "Copying Xstd shared library next to xli"
)
