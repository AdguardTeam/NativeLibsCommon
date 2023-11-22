cmake_minimum_required(VERSION 3.20)
project(cmake_wrapper)

include(conanbuildinfo.cmake)
conan_basic_setup()

if(WIN32)
    add_definitions(-DWIN32_LEAN_AND_MEAN=1)
endif()

add_subdirectory("source_subfolder")
