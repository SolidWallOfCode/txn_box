cmake_minimum_required(VERSION 3.12)
set(CMAKE_CXX_STANDARD 17)

set(INSTALL_DIR ${CMAKE_HOME_DIRECTORY})
include_directories(include)
link_directories(lib)

include(ExternalProject)
set(EP_BASE extern)

set(YAML_CPP_BUILD_TESTS off)
set(YAML_CPP_BUILD_TOOLS off)
set(YAML_CPP_BUILD_CONTRIB off)
ExternalProject_Add(YAML_CPP
    GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
    GIT_TAG yaml-cpp-0.6.2
    GIT_SHALLOW TRUE
    INSTALL_DIR ${INSTALL_DIR}
    CMAKE_ARGS -DCMAKE_INSTALL_PREFIX:PATH=${INSTALL_DIR}
    )

ExternalProject_Add(libswoc++
    GIT_REPOSITORY https://github.com/solidwallofcode/libswoc.git
    GIT_TAG cmake
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR libswoc++
    INSTALL_DIR ${INSTALL_DIR}
    CMAKE_ARGS -DCMAKE_INSTALL_PREFIX:PATH=${INSTALL_DIR}
    )

ExternalProject_Add(CannedYAML
    GIT_REPOSITORY https://github.com/solidwallofcode/canned-yaml.git
    GIT_TAG master
    GIT_SHALLOW TRUE
    INSTALL_DIR ${INSTALL_DIR}
    CMAKE_ARGS -DCMAKE_INSTALL_PREFIX:PATH=${INSTALL_DIR}
    )

ExternalProject_Add(ATS
    GIT_REPOSITORY https://github.com/apache/trafficserver.git
    GIT_TAG master
    GIT_SHALLOW TRUE
    INSTALL_DIR ${INSTALL_DIR}
    PATCH_COMMEND autoreconf -i <SOURCE_DIR>
    CONFIGURE_COMMAND <SOURCE_DIR>/configure --prefix=${INSTALL_DIR} --enable-debug 
    )

add_subdirectory(txn_box)

add_custom_target(clang-format)
add_dependencies(clang-format clang-format-txn-box)
add_dependencies(txn-box ATS CannedYAML libswoc++ YAML_CPP)
