# Tests CMakeLists.txt

# Enable testing
enable_testing()

# ----- Helper macro -------------------------------------------------------
macro(xstd_test target_name source_file)
    add_executable(${target_name} ${source_file})
    target_link_libraries(${target_name}
        PRIVATE
            Xstd
            GTest::gtest
            GTest::gtest_main
    )
    target_include_directories(${target_name}
        PRIVATE
            ${SRC_DIR}
            ${INCLUDE_DIR}
    )
    add_test(NAME ${target_name} COMMAND ${target_name})
endmacro()
# --------------------------------------------------------------------------

xstd_test(hasher_test      hasher_test.cc)
xstd_test(btree_test       btree_test.cc)
xstd_test(compression_test compression_test.cc)
xstd_test(page_manager_test page_manager_test.cc)
xstd_test(encryption_test  encryption_test.cc)
xstd_test(archive_test     archive_test.cc)

