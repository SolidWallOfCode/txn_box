cmake_minimum_required(VERSION 3.12)
project(test_txn_box CXX)
set(CMAKE_CXX_STANDARD 17)

add_executable(test_txn_box
    unit_test_main.cc

    test_txn_box.cc
    )

set_target_properties(test_txn_box PROPERTIES CLANG_FORMAT_DIRS ${CMAKE_CURRENT_SOURCE_DIR})

# After fighting with CMake over the include paths, it's just not worth it to be correct.
# target_link_libraries should make this work but it doesn't. I can't figure out why.
include_directories("../plugin/include")


