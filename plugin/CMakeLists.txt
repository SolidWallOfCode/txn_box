cmake_minimum_required(VERSION 3.11)

project(plugin CXX)
set(CMAKE_CXX_STANDARD 17)

add_library(plugin SHARED
	src/0_static.cc

	src/txn_box.cc
	src/txn_box_remap.cc

	src/Accelerator.cc
    	src/Comparison.cc
	src/Config.cc
	src/Context.cc
	src/Directive.cc
	src/Extractor.cc
	src/Ex_Base.cc
	src/Modifier.cc
	src/Machinery.cc
        src/Rxp.cc
	src/ts_util.cc
	src/util.cc
	src/yaml_util.cc

	src/Ex_HTTP.cc
	src/Ex_Ssn.cc
	src/ex_tcp_info.cc
    src/table_util.cc
	src/ip_space.cc
	src/str_table.cc
	src/query.cc
	src/stats.cc
	src/text_block.cc
	)
set_property(TARGET plugin PROPERTY PREFIX "")
set_property(TARGET plugin PROPERTY OUTPUT_NAME "txn_box")

target_link_libraries(plugin PRIVATE libswoc pcre2-8)
target_include_directories(plugin PUBLIC include)
target_include_directories(plugin PRIVATE ${PLUGIN_SOURCE_DIR}/include ${trafficserver_INCLUDE_DIRS} ${OPENSSL_INCLUDE_DIR}, libswoc)
if (CMAKE_COMPILER_IS_GNUCXX)
    target_compile_options(plugin PRIVATE -Wall -Wextra -Werror -Wnon-virtual-dtor)
endif()

install(TARGETS plugin LIBRARY DESTINATION ${INSTALL_DIR}/lib)
