cmake_minimum_required(VERSION 3.24)
project(ag_common_tls)

set(CMAKE_CXX_STANDARD 20)

set(NLC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/..)

set(SOURCE_FILES
        cert_utils.cpp
        make_ssl.cpp
)

add_library(${PROJECT_NAME} STATIC ${SOURCE_FILES})

target_include_directories(${PROJECT_NAME} PUBLIC include)

if (NOT TARGET ag_common)
    add_subdirectory(${NLC_DIR}/common ${CMAKE_BINARY_DIR}/common)
endif (NOT TARGET ag_common)

find_package(brotli REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(ngtcp2 REQUIRED)

target_link_libraries(${PROJECT_NAME} PUBLIC ag_common openssl::openssl)
target_link_libraries(${PROJECT_NAME} PRIVATE ngtcp2::ngtcp2 brotli::brotli)

if (MSVC)
    target_compile_definitions(${PROJECT_NAME} PRIVATE
            WIN32_LEAN_AND_MEAN
            NOMINMAX)
endif ()

enable_testing()
include(../cmake/add_unit_test.cmake)
set(TEST_DIR ${CMAKE_CURRENT_SOURCE_DIR}/test)

add_library(ja4 STATIC EXCLUDE_FROM_ALL test/ja4.cpp test/client_hello.cpp)
target_include_directories(ja4 PUBLIC test)
target_link_libraries(ja4 ag_common)

link_libraries(${PROJECT_NAME})
link_libraries(ja4)

add_unit_test(make_ssl_test ${TEST_DIR} "" TRUE TRUE)

# Integration test that compares the ClientHello of real browsers with the `make_ssl` profiles.
# It needs macOS, installed browsers and a GUI session; keep it out of `tests`/CTest so that
# `make test` and the pull-request CI do not build or run it.
option(NLC_CLIENTHELLO_TESTS "Build the macOS ClientHello integration test" OFF)
if (NLC_CLIENTHELLO_TESTS)
    if (NOT APPLE)
        message(FATAL_ERROR "NLC_CLIENTHELLO_TESTS is supported on macOS only")
    endif ()

    find_package(GTest REQUIRED)
    find_package(Threads REQUIRED)

    add_executable(clienthello_profiles_test EXCLUDE_FROM_ALL test/clienthello_profiles_test.cpp)
    target_link_libraries(clienthello_profiles_test PRIVATE gtest::gtest Threads::Threads)
    target_compile_definitions(clienthello_profiles_test PRIVATE
            CLIENTHELLO_TEST_DATA_DIR="${TEST_DIR}/data")

    add_custom_target(clienthello_tests DEPENDS clienthello_profiles_test)
endif (NLC_CLIENTHELLO_TESTS)
