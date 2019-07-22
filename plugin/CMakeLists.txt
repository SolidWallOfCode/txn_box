cmake_minimum_required(VERSION 3.11)

project(plugin)
set(CMAKE_CXX_STANDARD 17)
include(GNUInstallDirs)

find_package(swoc++ CONFIG REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)
#find_package(canned-yaml CONFIG REQUIRED)

add_library(plugin SHARED
	src/txn_box.cc
    	src/Comparison.cc
	src/Config.cc
	src/Context.cc
	src/Directive.cc
	src/Extractor.cc
        src/FeatureMod.cc
	src/Features.cc
	src/Machinery.cc
        src/Rxp.cc
	)
set_property(TARGET plugin PROPERTY PREFIX "")
set_property(TARGET plugin PROPERTY OUTPUT_NAME "txn_box")

target_link_libraries(plugin PUBLIC swoc++::swoc++ yaml-cpp pcre2-8)
target_include_directories(plugin PUBLIC include)
target_include_directories(plugin PRIVATE ${PLUGIN_SOURCE_DIR}/include)

install(TARGETS plugin LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
