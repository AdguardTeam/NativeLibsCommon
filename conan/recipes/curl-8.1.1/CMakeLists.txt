cmake_minimum_required(VERSION 2.8.0)
project(cmake_wrapper)

find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)
find_package(ngtcp2 REQUIRED)
find_package(nghttp2 REQUIRED)
find_package(nghttp3 REQUIRED)
find_package(ngtcp2 REQUIRED)
link_libraries(OpenSSL::SSL OpenSSL::Crypto ZLIB::ZLIB ngtcp2::ngtcp2 nghttp2::nghttp2 nghttp3::nghttp3)

add_subdirectory("source_subfolder")
