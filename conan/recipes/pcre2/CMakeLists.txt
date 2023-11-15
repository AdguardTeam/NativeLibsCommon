cmake_minimum_required(VERSION 3.1)
project(cmake_wrapper)

find_package(BZip2 REQUIRED)
find_package(ZLIB REQUIRED)
link_libraries(bzip2::bzip2 zlib::zlib)

add_subdirectory("source_subfolder")
