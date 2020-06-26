cmake_minimum_required(VERSION 3.12)
Project(TxnBox)
set(CMAKE_CXX_STANDARD 17)
include(GNUInstallDirs)
include(FindOpenSSL)

set(INSTALL_DIR ${CMAKE_HOME_DIRECTORY})
set(CMAKE_PREFIX_PATH ${INSTALL_DIR})
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_HOME_DIRECTORY}" CACHE PATH "..." FORCE)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(solid REQUIRED IMPORTED_TARGET libswoc.static)
pkg_check_modules(yaml-cpp REQUIRED IMPORTED_TARGET yaml-cpp)
pkg_check_modules(trafficserver REQUIRED IMPORTED_TARGET trafficserver)

add_subdirectory(plugin)
#add_subdirectory(test/unit_tests)
