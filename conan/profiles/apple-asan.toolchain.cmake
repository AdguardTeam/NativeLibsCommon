# We don't need to export any symbol implicitly, and also disable C++ exceptions.
set(CMAKE_C_FLAGS_INIT "${CMAKE_C_FLAGS_INIT} -fvisibility=hidden -fno-exceptions -fno-omit-frame-pointer -fsanitize=address")
set(CMAKE_CXX_FLAGS_INIT "${CMAKE_CXX_FLAGS_INIT} -fvisibility=hidden -fno-exceptions -fno-omit-frame-pointer -fsanitize=address")

add_compile_options("-fsanitize=address")
link_libraries("-fsanitize=address")
