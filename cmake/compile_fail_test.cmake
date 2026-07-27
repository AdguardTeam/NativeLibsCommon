# Script half of `add_compile_fail_test()` (see add_compile_fail_test.cmake), run as
#
#   cmake -D BUILD_DIR=... -D TARGET=... [-D CONFIG=...] [-D EXPECTED_REGEX=...]
#         -P compile_fail_test.cmake
#
# It builds `TARGET`, which is expected to fail to compile, and fails the test if the target built
# or if the compiler output does not match `EXPECTED_REGEX` (an empty regex checks nothing).

foreach (REQUIRED_VAR BUILD_DIR TARGET)
    if (NOT DEFINED ${REQUIRED_VAR})
        message(FATAL_ERROR "${REQUIRED_VAR} is not set")
    endif()
endforeach()

set(BUILD_COMMAND ${CMAKE_COMMAND} --build "${BUILD_DIR}" --target "${TARGET}")
if (NOT "${CONFIG}" STREQUAL "")
    # Ignored by the single-config generators the project's presets use.
    list(APPEND BUILD_COMMAND --config "${CONFIG}")
endif()

execute_process(COMMAND ${BUILD_COMMAND}
        RESULT_VARIABLE BUILD_RESULT
        OUTPUT_VARIABLE BUILD_STDOUT
        ERROR_VARIABLE BUILD_STDERR)
set(BUILD_OUTPUT "${BUILD_STDOUT}${BUILD_STDERR}")

if (BUILD_RESULT EQUAL 0)
    message(FATAL_ERROR "`${TARGET}` compiled successfully, but was expected to fail to compile.\n"
            "Build output:\n${BUILD_OUTPUT}")
endif()

if (NOT "${EXPECTED_REGEX}" STREQUAL "" AND NOT "${BUILD_OUTPUT}" MATCHES "${EXPECTED_REGEX}")
    message(FATAL_ERROR "`${TARGET}` failed to compile, as expected, but no diagnostic matched "
            "`${EXPECTED_REGEX}`.\nBuild output:\n${BUILD_OUTPUT}")
endif()

message(STATUS "`${TARGET}` failed to compile, as expected")
