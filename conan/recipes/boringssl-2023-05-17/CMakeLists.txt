cmake_minimum_required(VERSION 3.24)
project(cmake_wrapper)

if (WIN32)
# BoringSSL binaries need this, do not rely on CMake about linking to this library
link_libraries(advapi32)
endif ()
add_subdirectory("source_subfolder")
