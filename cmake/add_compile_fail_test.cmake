# Registers a CTest test that asserts a piece of code is *rejected* by the compiler.
#
# `ag::format()` and friends validate their format string in a `consteval` constructor, so an
# invalid format string is a compile error: there is nothing left to run, and the case cannot be
# expressed in a gtest binary. Such a case gets a target of its own instead, excluded from the
# build, and the test builds that target and expects the build to fail with an expected diagnostic.
#
#   add_compile_fail_test(<test-name> <source-file> <expected-regex> [<msvc-expected-regex>])
#
# `<expected-regex>` is matched against the compiler output, so a test fails both when the code
# compiles and when it fails for the wrong reason (a typo in the test source, say). Clang and GCC
# quote the offending source line, so the text passed to `on_error()` / `report_error()` appears in
# the diagnostic and the regex can be pinned to the exact error.
#
# MSVC reports a failed constant evaluation without quoting that line, so passing the same regex
# there would fail the test on Windows. Unless `<msvc-expected-regex>` says otherwise, the message
# check is therefore skipped under MSVC and only the compilation failure itself is asserted; the
# Clang and GCC runs are what pins the diagnostic down.
function(add_compile_fail_test TEST_NAME SOURCE_FILE EXPECTED_REGEX)
    if (NOT EXISTS "${SOURCE_FILE}")
        message(FATAL_ERROR "Cannot find source file for compile-fail test: ${TEST_NAME} (${SOURCE_FILE})")
    endif()

    if (MSVC)
        set(EXPECTED_REGEX "${ARGV3}")
    endif()

    # An object library: the code is never expected to make it as far as linking. Note that,
    # unlike a unit test, this target is deliberately not added to the `tests` target — `make test`
    # builds that one, and this target must not build.
    add_library(${TEST_NAME} OBJECT EXCLUDE_FROM_ALL ${SOURCE_FILE})

    add_test(NAME ${TEST_NAME} COMMAND ${CMAKE_COMMAND}
            -D "BUILD_DIR=${CMAKE_BINARY_DIR}"
            -D "TARGET=${TEST_NAME}"
            -D "CONFIG=$<CONFIG>"
            -D "EXPECTED_REGEX=${EXPECTED_REGEX}"
            -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/compile_fail_test.cmake")

    # Every such test shells out to the build system in the same build directory, so no two of
    # them may run at the same time, e.g. under `ctest -j`.
    set_tests_properties(${TEST_NAME} PROPERTIES RESOURCE_LOCK ag_compile_fail_build)
endfunction()
