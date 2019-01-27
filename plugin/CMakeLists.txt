cmake_minimum_required(VERSION 3.11)

project(txn-box)
set(CMAKE_CXX_STANDARD 17)
include(GNUInstallDirs)

find_package(swoc++ CONFIG REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)
find_package(canned-yaml CONFIG REQUIRED)

add_library(txn-box SHARED
	src/txn_box.cc
)
set_property(TARGET txn-box PROPERTY PREFIX "")
set_property(TARGET txn-box PROPERTY OUTPUT_NAME "txn_box")

target_link_libraries(txn-box PUBLIC swoc++::swoc++ yaml-cpp)

install(TARGETS txn-box LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
