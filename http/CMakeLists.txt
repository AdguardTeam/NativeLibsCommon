cmake_minimum_required(VERSION 3.24)
project(ag_common_http)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)

set(NLC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/..)

set(SOURCE_FILES
        headers.cpp
        http1.cpp
        http2.cpp
        http3.cpp
        util.cpp
)

add_library(${PROJECT_NAME} STATIC ${SOURCE_FILES})

target_include_directories(${PROJECT_NAME} PUBLIC include)

if (NOT TARGET ag_common)
    add_subdirectory(${NLC_DIR}/common ${CMAKE_BINARY_DIR}/common)
endif (NOT TARGET ag_common)

find_package(fmt REQUIRED)
find_package(magic_enum REQUIRED)
find_package(nghttp2 REQUIRED)
find_package(nghttp3 REQUIRED)
find_package(ngtcp2 REQUIRED)
find_package(llhttp REQUIRED)
find_package(libevent REQUIRED)
find_package(OpenSSL REQUIRED)

target_link_libraries(${PROJECT_NAME} PRIVATE ag_common)
target_link_libraries(${PROJECT_NAME} PRIVATE
        magic_enum::magic_enum
)

target_link_libraries(${PROJECT_NAME} PUBLIC
        fmt::fmt
        libevent::libevent
        llhttp::llhttp
        nghttp2::nghttp2
        nghttp3::nghttp3
        ngtcp2::ngtcp2
        openssl::openssl
)

if (MSVC)
    target_compile_definitions(${PROJECT_NAME} PRIVATE
            WIN32_LEAN_AND_MEAN
            NOMINMAX)
endif ()

enable_testing()
include(${NLC_DIR}/cmake/add_unit_test.cmake)
link_libraries(${PROJECT_NAME} ag_common)
set(TEST_DIR ${CMAKE_CURRENT_SOURCE_DIR}/test)

add_unit_test(headers_test ${TEST_DIR} "" TRUE TRUE)
add_unit_test(http1_client_test ${TEST_DIR} ${CMAKE_CURRENT_SOURCE_DIR} TRUE FALSE)
add_unit_test(http1_server_test ${TEST_DIR} ${CMAKE_CURRENT_SOURCE_DIR} TRUE FALSE)
add_unit_test(http2_client_test ${TEST_DIR} ${CMAKE_CURRENT_SOURCE_DIR} TRUE TRUE)
add_unit_test(http2_server_test ${TEST_DIR} ${CMAKE_CURRENT_SOURCE_DIR} TRUE TRUE)

add_library(http3_test_helper_lib STATIC ${TEST_DIR}/http3_server_side.cpp)
target_link_libraries(http3_test_helper_lib PRIVATE gtest::gtest)
link_libraries(http3_test_helper_lib)
add_unit_test(http3_test ${TEST_DIR} ${CMAKE_CURRENT_SOURCE_DIR} TRUE TRUE)
