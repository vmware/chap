# Copyright (c) 2017-2024 Broadcom. All Rights Reserved.
# The term "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.
# SPDX-License-Identifier: GPL-2.0

# We want to work on Ubuntu 16 LTS which uses CMake 3.5.
cmake_minimum_required (VERSION 3.5)

project(chap LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

# chap

add_subdirectory(thirdparty)

add_executable(chap src/FileAnalyzer.cpp)

# Replxx is  linked as a static library
target_link_libraries(chap PRIVATE Replxx::Replxx)
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
