cmake_minimum_required(VERSION 3.24)
if(NOT CMAKE_INSTALL_PREFIX MATCHES "conan2")
set(CMAKE_PROJECT_TOP_LEVEL_INCLUDES "cmake/conan_provider.cmake")
endif()
project(native_libs_common C CXX)

SET(CMAKE_CXX_STANDARD 20)
SET(CMAKE_C_STANDARD 11)
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded)

# Better stack traces in Sentry
if (MSVC)
    add_compile_options(/Oy-)

    set(CMAKE_CXX20_STANDARD_COMPILE_OPTION ${CMAKE_CXX20_STANDARD_COMPILE_OPTION} -Zc:char8_t- /MT)
    set(CMAKE_CXX20_EXTENSION_COMPILE_OPTION ${CMAKE_CXX20_EXTENSION_COMPILE_OPTION} -Zc:char8_t- /MT)

    set(CMAKE_C11_STANDARD_COMPILE_OPTION ${CMAKE_C11_STANDARD_COMPILE_OPTION} /MT)
    set(CMAKE_C11_EXTENSION_COMPILE_OPTION ${CMAKE_C11_EXTENSION_COMPILE_OPTION} /MT)
else ()
    add_compile_options(-fno-omit-frame-pointer)
endif ()

if(SANITIZE)
    add_compile_options("-fsanitize=address")
    link_libraries("-fsanitize=address")
endif(SANITIZE)

add_subdirectory(common)
add_subdirectory(http)

enable_testing()
