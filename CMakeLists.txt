cmake_minimum_required(VERSION 3.12)
set(CMAKE_CXX_STANDARD 17)

set(INSTALL_DIR ${CMAKE_HOME_DIRECTORY})
if(NOT DEFINED CMAKE_PREFIX_PATH)
    set(CMAKE_PREFIX_PATH ${INSTALL_DIR})
endif()

include(ExternalProject)
set(EP_BASE extern)

# All of the arguments to pass to external projects.
set(_CMAKE_ARGS
    CMAKE_INSTALL_PREFIX=${INSTALL_DIR}
    CMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH}
    YAML_CPP_BUILD_TESTS=off
    YAML_CPP_BUILD_TOOLS=off
    YAML_CPP_BUILD_CONTRIB=off
    )
list(TRANSFORM _CMAKE_ARGS PREPEND "-D")

ExternalProject_Add(lib-yaml-cpp
    GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
    GIT_TAG yaml-cpp-0.6.2
    GIT_SHALLOW TRUE
    INSTALL_DIR ${INSTALL_DIR}
    CMAKE_ARGS "${_CMAKE_ARGS}" -DCMAKE_CXX_FLAGS="-fPIC"
    STEP_TARGETS install
    )

ExternalProject_Add(lib-swoc++
    GIT_REPOSITORY https://github.com/solidwallofcode/libswoc.git
    GIT_TAG master
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR swoc++
    INSTALL_DIR ${INSTALL_DIR}
    CMAKE_ARGS "${_CMAKE_ARGS}" -DCMAKE_CXX_FLAGS="-fPIC"
    STEP_TARGETS install
    )

ExternalProject_Add(canned-yaml
    GIT_REPOSITORY https://github.com/solidwallofcode/canned-yaml.git
    GIT_TAG cmake
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR canner
    INSTALL_DIR ${INSTALL_DIR}
    CMAKE_ARGS "${_CMAKE_ARGS}"
    DEPENDS lib-yaml-cpp-install lib-swoc++-install
    STEP_TARGETS install
    )

# Riding serendipity - there's no package description for this, but because everything is installed
# in the same directories, the paths from the other packages makes this one work too.
ExternalProject_Add(traffic-server
    GIT_REPOSITORY https://github.com/apache/trafficserver.git
    GIT_TAG master
    GIT_SHALLOW TRUE
    INSTALL_DIR ${INSTALL_DIR}
    UPDATE_COMMAND autoreconf -i
    CONFIGURE_COMMAND <SOURCE_DIR>/configure --prefix ${CMAKE_HOME_DIRECTORY}
    BUILD_COMMAND make -j 8
    INSTALL_COMMAND make install
    STEP_TARGETS install
    )

# This has to be done manually, the first time or when the git tag is changed. It's that or a full
# rebuild *every time* the plugin is rebuilt. At some point I think I'll rig something that tracks
# the last download commit SHA and only configures if that's changed. As usual with CMake, it seems
# like supporting an autotools based project would be well trod ground with cookbook solutions, but
# I can't find any.

# Note one thing - actually building isn't needed, a configure suffices to get the required files
# in the proper state for use (it's just the headers, and there's no linking during plugin build).

ExternalProject_Add_Step(traffic-server rebuild
    COMMAND autoreconf -i <SOURCE_DIR> COMMAND <SOURCE_DIR>/configure --prefix=${INSTALL_DIR} --enable-debug COMMAND make COMMAND make install
    DEPENDEES DOWNLOAD
    EXCLUDE_FROM_MAIN TRUE
    )
ExternalProject_Add_StepTargets(traffic-server rebuild)

#ExternalProject_Add(plugin
#    SOURCE_DIR ${CMAKE_HOME_DIRECTORY}/plugin
#    INSTALL_DIR ${INSTALL_DIR}
#    CMAKE_ARGS "${_CMAKE_ARGS}"
#    DEPENDS canned-yaml-install
#    STEP_TARGETS install
#    )

# If using CLion, pass "-DENABLE_IDE_HACKS=TRUE" as a CMake argument to make the editor behave as desired.
if(${ENABLE_IDE_HACKS})
    add_subdirectory(plugin)
endif()
