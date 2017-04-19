# Copyright (c) 2017 VMware, Inc. All Rights Reserved.
# SPDX-License-Identifier: GPL-2.0

# We want to work on Ubuntu 16 LTS which uses CMake 3.5.
cmake_minimum_required (VERSION 3.5)

project(chap LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

# chap

# Add an "$ORIGIN/" RPATH so that we can "cross compile" against an alternate
# libstc++/libc++ as long as it is staged to the chap install directory.
set(CMAKE_INSTALL_RPATH "$ORIGIN/")

add_executable(chap src/FileAnalyzer.cpp)
install(TARGETS chap DESTINATION bin)

# Tests

add_subdirectory(test/expectedOutput)

# Add a 'check' target that depends on chap and dumps output on failure. It
# seems like this should be possible without a custom target, but attempts to
# use CTEST_OUTPUT_ON_FAILURE failed, as did attempts set DEPENDS on the tests
# directly.

if (CMAKE_CONFIGURATION_TYPES)
    add_custom_target(check
        COMMAND ${CMAKE_CTEST_COMMAND}
            --force-new-ctest-process --output-on-failure
            --build-config "$<CONFIGURATION>"
        DEPENDS chap
    )
else()
    add_custom_target(check
        COMMAND ${CMAKE_CTEST_COMMAND}
            --force-new-ctest-process --output-on-failure
        DEPENDS chap
    )
endif()
