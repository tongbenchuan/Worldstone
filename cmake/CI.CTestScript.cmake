# It might be better than calling 'make Continuous' for CI such as travis since it will skip the update step while still doing the later steps
# It is also makes it easier to customize the test runs and show the output since we can use command-line arguments
set(CTEST_SOURCE_DIRECTORY "./")
set(CTEST_BINARY_DIRECTORY "./buildCI")

set(CTEST_CMAKE_GENERATOR "Unix Makefiles")
#set(CTEST_USE_LAUNCHERS 1)

set(CTEST_BUILD_CONFIGURATION "Coverage") # Only works for multi-config generators

if(NOT "$ENV{GCOV}" STREQUAL "")
    set(GCOV_NAME "$ENV{GCOV}")
else()
    set(GCOV_NAME "gcov")
endif()

find_program(CTEST_COVERAGE_COMMAND NAMES "${GCOV_NAME}")

#set(CTEST_MEMORYCHECK_COMMAND "valgrind")
#set(CTEST_MEMORYCHECK_TYPE "ThreadSanitizer")

ctest_start("Continuous")
ctest_configure(OPTIONS -DCMAKE_BUILD_TYPE=${CTEST_BUILD_CONFIGURATION})
# Done by default when not using a script... Do it here since the CTestCustom.cmake file is generated by our CMakeLists.
ctest_read_custom_files( ${CTEST_BINARY_DIRECTORY} )
ctest_build()
ctest_test(RETURN_VALUE RET_VAL_TEST)
if(RET_VAL_TEST)
    message(FATAL_ERROR "Some tests failed !")
endif()
if(CTEST_COVERAGE_COMMAND)
    # There's no way to exclude files/directories from being ran with gcov,
    # as it can fail and it is not important since coverage will work,
    # just output a message instead of failing
    ctest_coverage(CAPTURE_CMAKE_ERROR RET_VAL_COV)
    if(RET_VAL_COV)
        message(STATUS "Coverage returned ${RET_VAL_COV}")
    endif()
else()
    message(WARNING "GCov not found, not running coverage")
endif()
#ctest_memcheck()
#ctest_submit() # Comment this if you want to use the script but not use CDash