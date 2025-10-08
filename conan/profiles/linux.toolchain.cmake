# We don't need to export any symbol implicitly, and also disable C++ exceptions, and enable unwind tables.
set(CMAKE_SYSTEM_NAME Linux)
set(CONAN_C_FLAGS "-fvisibility=hidden -fno-exceptions -funwind-tables -fno-omit-frame-pointer")
set(CONAN_CXX_FLAGS "-fvisibility=hidden -fno-exceptions -funwind-tables -fno-omit-frame-pointer")
