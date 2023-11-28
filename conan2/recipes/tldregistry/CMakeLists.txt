cmake_minimum_required(VERSION 3.6)
project(tldregistry CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(tldregistry STATIC
        src/tldregistry.cpp
        chromium/net/base/lookup_string_in_fixed_set.cc
        )

target_include_directories(tldregistry PUBLIC include)
target_include_directories(tldregistry PRIVATE chromium)

set_property(TARGET tldregistry PROPERTY POSITION_INDEPENDENT_CODE ON)

enable_testing()
add_custom_target(tests)

link_libraries(tldregistry)

add_executable(tldregistry_test EXCLUDE_FROM_ALL src/tldregistry_test.cpp)
add_dependencies(tests tldregistry_test)
add_test(tldregistry_test tldregistry_test)
