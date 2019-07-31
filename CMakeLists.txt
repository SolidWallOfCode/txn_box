cmake_minimum_required(VERSION 3.12)
set(CMAKE_CXX_STANDARD 17)

set(INSTALL_DIR ${CMAKE_HOME_DIRECTORY})
if(NOT DEFINED CMAKE_PREFIX_PATH)
    set(CMAKE_PREFIX_PATH ${INSTALL_DIR})
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(yaml-cpp REQUIRED IMPORTED_TARGET yaml-cpp)
pkg_check_modules(libswoc++ REQUIRED IMPORTED_TARGET libswoc++)
pkg_check_modules(trafficserver REQUIRED IMPORTED_TARGET trafficserver)

# If using CLion, pass "-DENABLE_IDE_HACKS=TRUE" as a CMake argument to make the editor behave as desired.
if(${ENABLE_IDE_HACKS})
    add_subdirectory(plugin)
endif()
