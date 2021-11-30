cmake_minimum_required(VERSION 3.10)
project(native_libs_common)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

set(COMMON_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(COMMON_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)

message(${COMMON_DIR})

include(cmake/conan_bootstrap.cmake)
conan_bootstrap(SRCROOT "${COMMON_DIR}" CONANFILE "${COMMON_DIR}/conanfile.py" SCOPE_NAME commonlibs)

set(SOURCE_FILES
        utils.cpp
        socket_address.cpp
        logger.cpp
        time_utils.cpp
        clock.cpp
        )

add_library(${PROJECT_NAME} STATIC EXCLUDE_FROM_ALL ${SOURCE_FILES})

target_include_directories(${PROJECT_NAME} PUBLIC include)

target_compile_definitions(${PROJECT_NAME} PUBLIC PCRE2_STATIC=1 PCRE2_CODE_UNIT_WIDTH=8)
target_link_libraries(${PROJECT_NAME} CONAN_PKG::fmt CONAN_PKG::libevent
        CONAN_PKG::pcre2)
target_compile_options(${PROJECT_NAME}  PUBLIC -Wno-format-nonliteral) # for fmt's chrono build
if (NOT MSVC)
    target_compile_options(${PROJECT_NAME}  PRIVATE -fno-exceptions)
endif()
target_compile_definitions(${PROJECT_NAME}  PUBLIC FMT_EXCEPTIONS=0)

enable_testing()
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/add_unit_test.cmake)
link_libraries(native_libs_common)
set(TEST_DIR ${CMAKE_CURRENT_SOURCE_DIR}/test)

add_unit_test(utils_test ${TEST_DIR} "" TRUE TRUE)
add_unit_test(logger_test ${TEST_DIR} "" TRUE TRUE)
add_unit_test(time_utils_test ${TEST_DIR} "" TRUE TRUE)
add_unit_test(cache_test ${TEST_DIR} "" TRUE TRUE)
