cmake_minimum_required(VERSION 3.10)
project(native_libs_common)

# Better stack traces in Sentry
if (MSVC)
    add_compile_options(/Oy-)
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
