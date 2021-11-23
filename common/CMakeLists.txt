cmake_minimum_required(VERSION 3.10)
project(native_libs_common)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

set(COMMON_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(COMMON_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)

message(${COMMON_DIR})

include(cmake/conan_bootstrap.cmake)
conan_bootstrap(SRCROOT "${COMMON_DIR}" CONANFILE "${COMMON_DIR}/conanfile.txt" SCOPE_NAME commonlibs)

set(SOURCE_FILES
        ${COMMON_DIR}/utils.cpp
        ${COMMON_DIR}/socket_address.cpp
        )

add_library(${PROJECT_NAME} STATIC EXCLUDE_FROM_ALL ${SOURCE_FILES})

target_compile_options(${PROJECT_NAME}  PUBLIC -Wno-format-nonliteral) # for fmt's chrono build

target_include_directories(${PROJECT_NAME} PUBLIC include)

target_link_libraries(${PROJECT_NAME} CONAN_PKG::fmt CONAN_PKG::libevent)

enable_testing()
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/add_unit_test.cmake)
link_libraries(native_libs_common)
set(TEST_DIR ${CMAKE_CURRENT_SOURCE_DIR}/test)

add_unit_test(utils_test ${TEST_DIR} "" TRUE TRUE)