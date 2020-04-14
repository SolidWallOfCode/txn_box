cmake_minimum_required(VERSION 3.11)

project(plugin CXX)
set(CMAKE_CXX_STANDARD 17)

add_library(plugin SHARED
	src/txn_box.cc
	src/txn_box_remap.cc
	src/Accelerator.cc
    	src/Comparison.cc
	src/Config.cc
	src/Context.cc
	src/Directive.cc
	src/Extractor.cc
	src/Modifier.cc
	src/Features.cc
	src/Machinery.cc
        src/Rxp.cc
	src/TextBlock.cc
	src/ts_util.cc
	src/yaml_util.cc
	)
set_property(TARGET plugin PROPERTY PREFIX "")
set_property(TARGET plugin PROPERTY OUTPUT_NAME "txn_box")

#target_link_libraries(plugin PUBLIC PkgConfig::libswoc++ PkgConfig::yaml-cpp pcre2-8)
target_link_libraries(plugin PUBLIC PkgConfig::libswoc pcre2-8)
target_include_directories(plugin PUBLIC include)
target_include_directories(plugin PRIVATE ${PLUGIN_SOURCE_DIR}/include ${trafficserver_INCLUDE_DIRS} ${OPENSSL_INCLUDE_DIR})

install(TARGETS plugin LIBRARY DESTINATION ${INSTALL_DIR}/lib)
