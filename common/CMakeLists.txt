cmake_minimum_required(VERSION 3.10)
project(ag_common)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

include(../cmake/conan_bootstrap.cmake)
conan_bootstrap(SRCROOT ".." CONANFILE "../conanfile.py" SCOPE_NAME native_libs_common)

set(SOURCE_FILES
        utils.cpp
        socket_address.cpp
        logger.cpp
        time_utils.cpp
        clock.cpp
        regex.cpp
        file.cpp
        base64.cpp
        cesu8.cpp
        )

add_library(${PROJECT_NAME} STATIC ${SOURCE_FILES})

target_include_directories(${PROJECT_NAME} PUBLIC include)

target_compile_definitions(${PROJECT_NAME} PUBLIC PCRE2_STATIC=1 PCRE2_CODE_UNIT_WIDTH=8)
target_link_libraries(${PROJECT_NAME} CONAN_PKG::fmt CONAN_PKG::libevent
        CONAN_PKG::pcre2)
target_compile_options(${PROJECT_NAME}  PUBLIC -Wno-format-nonliteral) # for fmt's chrono build
if (NOT MSVC)
    target_compile_options(${PROJECT_NAME}  PRIVATE -fno-exceptions)
else ()
    target_compile_definitions(${PROJECT_NAME} PRIVATE
            WIN32_LEAN_AND_MEAN
            NOMINMAX)
endif()
target_compile_definitions(${PROJECT_NAME}  PUBLIC FMT_EXCEPTIONS=0)

enable_testing()
include(../cmake/add_unit_test.cmake)
link_libraries(${PROJECT_NAME})
set(TEST_DIR ${CMAKE_CURRENT_SOURCE_DIR}/test)

add_unit_test(utils_test ${TEST_DIR} "" TRUE TRUE)
add_unit_test(logger_test ${TEST_DIR} "" TRUE TRUE)
add_unit_test(time_utils_test ${TEST_DIR} "" TRUE TRUE)
add_unit_test(cache_test ${TEST_DIR} "" TRUE TRUE)
