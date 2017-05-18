# Copyright (c) 2017 VMware, Inc. All Rights Reserved.
# SPDX-License-Identifier: GPL-2.0

# Configure expectedOutput tests.
#
# Each test directory contains:
#
# - A script called "run", which runs chap in some way. Comments in the script
#   should describe what the test does in terms of the inputs and expected
#   output.
#
# - A core file.
#
# - Possibly some other files considered to be input to chap. This is less
#   common, but might for example, include a .symdefs corresponding to the core
#   file.
#
# - Files created by some previous run of chap that was thought to be correct.
#   Often these will be files created as a result of running chap with redirect
#   on. All such files should be text files.
#
# Running ./run within the test directory should produce an identical set of
# files -- git should see no modified files.
#
# The test framework will copy the run script and any input files to a directory
# in the build tree, execute run, and then compare the build tree directory to
# the source directory. The test passes IFF the two directories have the same
# contents.

function(exout_test)
    set(options)
    set(valueArgs PATH)
    set(listArgs FILES)
    cmake_parse_arguments(
        EXOUT "${options}" "${valueArgs}" "${listArgs}" ${ARGN} )

    # Configure files to be copied, including implicit "run".
    foreach(exout_file ${EXOUT_FILES};run)
        configure_file(${EXOUT_PATH}/${exout_file}
                       ${EXOUT_PATH}/${exout_file} COPYONLY)
    endforeach(exout_file)

    # Configure the test.
    add_test(
        NAME expectedOutput/${EXOUT_PATH}
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/driver
                $<TARGET_FILE:chap>
                ${CMAKE_CURRENT_SOURCE_DIR}/${EXOUT_PATH}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/${EXOUT_PATH}
    )
endfunction(exout_test)

# Call exout_test for each test directory listing the files that need to be
# copied, modulo "run" which is assumed.

exout_test(PATH ELF32/LibcMalloc/OneAllocated FILES core.2088)
exout_test(PATH ELF64/LibcMalloc/OneAllocated FILES core.48555)
exout_test(PATH ELF32/LibcMalloc/OneLeaked FILES core.51504)
exout_test(PATH ELF64/LibcMalloc/OneLeaked FILES core.20675)
exout_test(PATH ELF64/LibcMalloc/HasSymbols_CoreHasMangledTypeNames_NoSymdefs
           FILES core.34218)
