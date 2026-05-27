cmake_minimum_required(VERSION 3.24)
project(ag_common_tls)

set(CMAKE_CXX_STANDARD 20)

set(NLC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/..)

set(SOURCE_FILES
        cert_utils.cpp
)

add_library(${PROJECT_NAME} STATIC ${SOURCE_FILES})

target_include_directories(${PROJECT_NAME} PUBLIC include)

find_package(OpenSSL REQUIRED)

target_link_libraries(${PROJECT_NAME} PUBLIC openssl::openssl)
