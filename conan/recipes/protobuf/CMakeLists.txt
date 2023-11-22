cmake_minimum_required(VERSION 3.0)
project(protobuf C CXX)
include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup()

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

set(PROTOBUF_BASE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/protobuf-3.18.0/src)
set(PROTOBUF_SRC_DIR ${PROTOBUF_BASE_DIR}/google/protobuf)
set(PROTOBUF_SRC_IO_DIR ${PROTOBUF_SRC_DIR}/io)
set(PROTOBUF_SRC_STUBS_DIR ${PROTOBUF_SRC_DIR}/stubs)

set(SOURCES
        ${PROTOBUF_SRC_DIR}/any.cc
        ${PROTOBUF_SRC_DIR}/any_lite.cc
        ${PROTOBUF_SRC_DIR}/arena.cc
        ${PROTOBUF_SRC_DIR}/arenastring.cc
        ${PROTOBUF_SRC_DIR}/descriptor.cc
        ${PROTOBUF_SRC_DIR}/descriptor_database.cc
        ${PROTOBUF_SRC_DIR}/descriptor.pb.cc
        ${PROTOBUF_SRC_DIR}/dynamic_message.cc
        ${PROTOBUF_SRC_DIR}/extension_set.cc
        ${PROTOBUF_SRC_DIR}/extension_set_heavy.cc
        ${PROTOBUF_SRC_DIR}/generated_message_reflection.cc
        ${PROTOBUF_SRC_DIR}/generated_message_util.cc
        ${PROTOBUF_SRC_DIR}/implicit_weak_message.cc
        ${PROTOBUF_SRC_DIR}/map.cc
        ${PROTOBUF_SRC_DIR}/map_field.cc
        ${PROTOBUF_SRC_DIR}/message.cc
        ${PROTOBUF_SRC_DIR}/message_lite.cc
        ${PROTOBUF_SRC_DIR}/parse_context.cc
        ${PROTOBUF_SRC_DIR}/reflection_ops.cc
        ${PROTOBUF_SRC_DIR}/repeated_field.cc
        ${PROTOBUF_SRC_DIR}/text_format.cc
        ${PROTOBUF_SRC_DIR}/unknown_field_set.cc
        ${PROTOBUF_SRC_DIR}/wire_format.cc
        ${PROTOBUF_SRC_DIR}/wire_format_lite.cc

        ${PROTOBUF_SRC_IO_DIR}/coded_stream.cc
        ${PROTOBUF_SRC_IO_DIR}/strtod.cc
        ${PROTOBUF_SRC_IO_DIR}/tokenizer.cc
        ${PROTOBUF_SRC_IO_DIR}/zero_copy_stream.cc
        ${PROTOBUF_SRC_IO_DIR}/zero_copy_stream_impl.cc
        ${PROTOBUF_SRC_IO_DIR}/zero_copy_stream_impl_lite.cc

        ${PROTOBUF_SRC_STUBS_DIR}/common.cc
        ${PROTOBUF_SRC_STUBS_DIR}/status.cc
        ${PROTOBUF_SRC_STUBS_DIR}/stringpiece.cc
        ${PROTOBUF_SRC_STUBS_DIR}/stringprintf.cc
        ${PROTOBUF_SRC_STUBS_DIR}/structurally_valid.cc
        ${PROTOBUF_SRC_STUBS_DIR}/strutil.cc
        ${PROTOBUF_SRC_STUBS_DIR}/substitute.cc
        ${PROTOBUF_SRC_STUBS_DIR}/int128.cc
)

if(WIN32)
    list(APPEND SOURCES
            ${PROTOBUF_SRC_IO_DIR}/io_win32.cc
    )
endif()

add_library(protobuf STATIC EXCLUDE_FROM_ALL ${SOURCES})
set_property(TARGET protobuf PROPERTY POSITION_INDEPENDENT_CODE ON)
target_include_directories(protobuf PUBLIC ${PROTOBUF_BASE_DIR})

if(NOT WIN32)
    add_definitions(-DHAVE_PTHREAD)
endif()
