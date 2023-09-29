cmake_minimum_required(VERSION 3.10)
project(ag_common_http)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)

set(NLC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/..)

include(${NLC_DIR}/cmake/conan_bootstrap.cmake)
conan_bootstrap(SRCROOT ".." CONANFILE "../conanfile.py" SCOPE_NAME native_libs_common)

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

target_link_libraries(${PROJECT_NAME} PRIVATE ag_common)
target_link_libraries(${PROJECT_NAME} PRIVATE
        CONAN_PKG::magic_enum
)

target_link_libraries(${PROJECT_NAME} PUBLIC
        CONAN_PKG::fmt
        CONAN_PKG::libevent
        CONAN_PKG::llhttp
        CONAN_PKG::nghttp2
        CONAN_PKG::nghttp3
        CONAN_PKG::ngtcp2
        CONAN_PKG::openssl
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
target_link_libraries(http3_test_helper_lib PRIVATE CONAN_PKG::gtest)
link_libraries(http3_test_helper_lib)
add_unit_test(http3_test ${TEST_DIR} ${CMAKE_CURRENT_SOURCE_DIR} TRUE TRUE)
