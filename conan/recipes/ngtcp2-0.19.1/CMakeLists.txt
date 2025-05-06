cmake_minimum_required(VERSION 3.24)
project(cmake_wrapper)

find_package(OpenSSL REQUIRED)
link_libraries(openssl::openssl)

if(WIN32)
    add_definitions(-DWIN32_LEAN_AND_MEAN=1)
endif()

add_subdirectory("source_subfolder")
