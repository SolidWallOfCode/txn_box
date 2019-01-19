cmake_minimum_required(VERSION 3.11)

set(CMAKE_CXX_STANDARD 17)

project(txn-box)

include_directories(include)

add_library(txn-box SHARED
	src/txn_box.cc
)

install(TARGETS txn-box LIBRARY DESTINATION lib)

add_custom_target(clang-format-txn-box COMMAND ${CMAKE_HOME_DIRECTORY}/tools/clang-format.sh src)
