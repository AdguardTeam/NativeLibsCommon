cmake_minimum_required(VERSION 3.24)
project(ag_common)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)

set(SOURCE_FILES
        utils.cpp
        socket_address.cpp
        logger.cpp
        time_utils.cpp
        clock.cpp
        regex.cpp
        file.cpp
        base64.cpp
        cesu8.cpp
        route_resolver.cpp
        net_utils.cpp
        error.cpp
        url.cpp
        rotating_log_to_file.cpp
)

add_library(${PROJECT_NAME} STATIC ${SOURCE_FILES})

find_package(fmt REQUIRED)
find_package(libevent REQUIRED)
find_package(pcre2 REQUIRED)

target_include_directories(${PROJECT_NAME} PUBLIC include)
target_compile_definitions(${PROJECT_NAME} PUBLIC PCRE2_STATIC=1 PCRE2_CODE_UNIT_WIDTH=8)
target_link_libraries(${PROJECT_NAME} fmt::fmt libevent::libevent
        pcre2::pcre2)
if (NOT MSVC)
    target_compile_options(${PROJECT_NAME}  PUBLIC -Wno-format-nonliteral) # for fmt's chrono build
    target_compile_options(${PROJECT_NAME}  PRIVATE -fno-exceptions)
else ()
    target_compile_definitions(${PROJECT_NAME} PRIVATE
            WIN32_LEAN_AND_MEAN
            NOMINMAX)
    target_link_libraries(${PROJECT_NAME} Iphlpapi.lib Ntdll.lib)
endif()
target_compile_definitions(${PROJECT_NAME}  PUBLIC FMT_EXCEPTIONS=0)

enable_testing()
include(../cmake/add_unit_test.cmake)
link_libraries(${PROJECT_NAME})
set(TEST_DIR ${CMAKE_CURRENT_SOURCE_DIR}/test)

add_unit_test(utils_test ${TEST_DIR} "" TRUE TRUE)
add_unit_test(logger_test ${TEST_DIR} "" TRUE TRUE)
add_unit_test(time_utils_test ${TEST_DIR} "" TRUE TRUE)
add_unit_test(cache_test ${TEST_DIR} "" TRUE TRUE)
add_unit_test(error_test ${TEST_DIR} "" TRUE TRUE)
add_unit_test(cidr_range_test ${TEST_DIR} "" TRUE TRUE)
add_unit_test(coro_test ${TEST_DIR} "" TRUE TRUE)
add_unit_test(net_utils_test ${TEST_DIR} "" TRUE TRUE)
add_unit_test(base64_test ${TEST_DIR} "" TRUE TRUE)
add_unit_test(rotating_log_to_file_test ${TEST_DIR} "" TRUE TRUE)
add_unit_test(regex_test ${TEST_DIR} "" TRUE TRUE)
