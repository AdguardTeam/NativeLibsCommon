set(CONAN_MINIMUM_VERSION 2.0.5)


function(detect_os OS OS_API_LEVEL OS_SDK OS_SUBSYSTEM OS_VERSION)
    # it could be cross compilation
    message(STATUS "CMake-Conan: cmake_system_name=${CMAKE_SYSTEM_NAME}")
    if(CMAKE_SYSTEM_NAME AND NOT CMAKE_SYSTEM_NAME STREQUAL "Generic")
        if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
            set(${OS} Macos PARENT_SCOPE)
        elseif(CMAKE_SYSTEM_NAME STREQUAL "QNX")
            set(${OS} Neutrino PARENT_SCOPE)
        elseif(CMAKE_SYSTEM_NAME STREQUAL "CYGWIN")
            set(${OS} Windows PARENT_SCOPE)
            set(${OS_SUBSYSTEM} cygwin PARENT_SCOPE)
        elseif(CMAKE_SYSTEM_NAME MATCHES "^MSYS")
            set(${OS} Windows PARENT_SCOPE)
            set(${OS_SUBSYSTEM} msys2 PARENT_SCOPE)
        else()
            set(${OS} ${CMAKE_SYSTEM_NAME} PARENT_SCOPE)
        endif()
        if(CMAKE_SYSTEM_NAME STREQUAL "Android")
            if(DEFINED ANDROID_PLATFORM)
                string(REGEX MATCH "[0-9]+" _OS_API_LEVEL ${ANDROID_PLATFORM})
            elseif(DEFINED CMAKE_SYSTEM_VERSION)
                set(_OS_API_LEVEL ${CMAKE_SYSTEM_VERSION})
            endif()
            message(STATUS "CMake-Conan: android api level=${_OS_API_LEVEL}")
            set(${OS_API_LEVEL} ${_OS_API_LEVEL} PARENT_SCOPE)
        endif()
        if(CMAKE_SYSTEM_NAME MATCHES "Darwin|iOS|tvOS|watchOS")
            # CMAKE_OSX_SYSROOT contains the full path to the SDK for MakeFile/Ninja
            # generators, but just has the original input string for Xcode.
            if(NOT IS_DIRECTORY ${CMAKE_OSX_SYSROOT})
                set(_OS_SDK ${CMAKE_OSX_SYSROOT})
            else()
                if(CMAKE_OSX_SYSROOT MATCHES Simulator)
                    set(apple_platform_suffix simulator)
                else()
                    set(apple_platform_suffix os)
                endif()
                if(CMAKE_OSX_SYSROOT MATCHES AppleTV)
                    set(_OS_SDK "appletv${apple_platform_suffix}")
                elseif(CMAKE_OSX_SYSROOT MATCHES iPhone)
                    set(_OS_SDK "iphone${apple_platform_suffix}")
                elseif(CMAKE_OSX_SYSROOT MATCHES Watch)
                    set(_OS_SDK "watch${apple_platform_suffix}")
                endif()
            endif()
            if(DEFINED _OS_SDK)
                message(STATUS "CMake-Conan: cmake_osx_sysroot=${CMAKE_OSX_SYSROOT}")
                set(${OS_SDK} ${_OS_SDK} PARENT_SCOPE)
            endif()
            if(DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
                message(STATUS "CMake-Conan: cmake_osx_deployment_target=${CMAKE_OSX_DEPLOYMENT_TARGET}")
                set(${OS_VERSION} ${CMAKE_OSX_DEPLOYMENT_TARGET} PARENT_SCOPE)
            endif()
        endif()
    endif()
endfunction()


function(detect_arch ARCH)
    # CMAKE_OSX_ARCHITECTURES can contain multiple architectures, but Conan only supports one.
    # Therefore this code only finds one. If the recipes support multiple architectures, the
    # build will work. Otherwise, there will be a linker error for the missing architecture(s).
    if(DEFINED CMAKE_OSX_ARCHITECTURES)
        string(REPLACE " " ";" apple_arch_list "${CMAKE_OSX_ARCHITECTURES}")
        list(LENGTH apple_arch_list apple_arch_count)
        if(apple_arch_count GREATER 1)
            message(WARNING "CMake-Conan: Multiple architectures detected, this will only work if Conan recipe(s) produce fat binaries.")
        endif()
    endif()
    if(CMAKE_SYSTEM_NAME MATCHES "Darwin|iOS|tvOS|watchOS")
        set(host_arch ${CMAKE_OSX_ARCHITECTURES})
    elseif(MSVC)
        set(host_arch ${CMAKE_CXX_COMPILER_ARCHITECTURE_ID})
    else()
        set(host_arch ${CMAKE_SYSTEM_PROCESSOR})
    endif()
    if(host_arch MATCHES "aarch64|arm64|ARM64")
        set(_ARCH armv8)
    elseif(host_arch MATCHES "armv7|armv7-a|armv7l|ARMV7")
        set(_ARCH armv7)
    elseif(host_arch MATCHES armv7s)
        set(_ARCH armv7s)
    elseif(host_arch MATCHES "i686|i386|X86")
        set(_ARCH x86)
    elseif(host_arch MATCHES "AMD64|amd64|x86_64|x64")
        set(_ARCH x86_64)
    elseif(host_arch MATCHES "mipsel")
        set(_ARCH mipsel)
    elseif(host_arch MATCHES "mips")
        set(_ARCH mips)
    endif()
    message(STATUS "CMake-Conan: cmake_system_processor=${_ARCH}")
    set(${ARCH} ${_ARCH} PARENT_SCOPE)
endfunction()


function(detect_cxx_standard CXX_STANDARD)
    set(${CXX_STANDARD} ${CMAKE_CXX_STANDARD} PARENT_SCOPE)
    if(CMAKE_CXX_EXTENSIONS)
        set(${CXX_STANDARD} "gnu${CMAKE_CXX_STANDARD}" PARENT_SCOPE)
    endif()
endfunction()


macro(detect_gnu_libstdcxx)
    # _CONAN_IS_GNU_LIBSTDCXX true if GNU libstdc++
    check_cxx_source_compiles("
    #include <cstddef>
    #if !defined(__GLIBCXX__) && !defined(__GLIBCPP__)
    static_assert(false);
    #endif
    int main(){}" _CONAN_IS_GNU_LIBSTDCXX)

    # _CONAN_GNU_LIBSTDCXX_IS_CXX11_ABI true if C++11 ABI
    check_cxx_source_compiles("
    #include <string>
    static_assert(sizeof(std::string) != sizeof(void*), \"using libstdc++\");
    int main () {}" _CONAN_GNU_LIBSTDCXX_IS_CXX11_ABI)

    set(_CONAN_GNU_LIBSTDCXX_SUFFIX "")
    if(_CONAN_GNU_LIBSTDCXX_IS_CXX11_ABI)
        set(_CONAN_GNU_LIBSTDCXX_SUFFIX "11")
    endif()
    unset (_CONAN_GNU_LIBSTDCXX_IS_CXX11_ABI)
endmacro()


macro(detect_libcxx)
    # _CONAN_IS_LIBCXX true if LLVM libc++
    check_cxx_source_compiles("
    #include <cstddef>
    #if !defined(_LIBCPP_VERSION)
       static_assert(false);
    #endif
    int main(){}" _CONAN_IS_LIBCXX)
endmacro()


# Split CMAKE_<LANG>_COMPILER into the executable and its arguments.
# The compiler may be given as a list (`zig;cc;-target;x86_64-linux-musl`), which
# CMake splits into CMAKE_<LANG>_COMPILER plus a space-separated
# CMAKE_<LANG>_COMPILER_ARG1 once the language has been enabled. Handle both forms,
# as this may be called before or after that split has happened.
function(split_compiler_command LANG EXECUTABLE ARGS)
    set(_command "${CMAKE_${LANG}_COMPILER}")
    list(LENGTH _command _command_length)
    if(_command_length GREATER 1)
        list(GET _command 0 _executable)
        list(SUBLIST _command 1 -1 _args)
    else()
        set(_executable "${_command}")
        string(STRIP "${CMAKE_${LANG}_COMPILER_ARG1}" _args_string)
        if(_args_string STREQUAL "")
            set(_args "")
        else()
            separate_arguments(_args NATIVE_COMMAND "${_args_string}")
        endif()
    endif()
    set(${EXECUTABLE} "${_executable}" PARENT_SCOPE)
    set(${ARGS} "${_args}" PARENT_SCOPE)
endfunction()


# ZIG_EXECUTABLE/ZIG_ARGS are set only when LANG is compiled by zig
# (i.e. `zig cc ...` or `zig c++ ...`); both are empty otherwise.
function(detect_zig_compiler LANG ZIG_EXECUTABLE ZIG_ARGS)
    set(${ZIG_EXECUTABLE} "" PARENT_SCOPE)
    set(${ZIG_ARGS} "" PARENT_SCOPE)

    split_compiler_command(${LANG} _executable _args)
    if(NOT _executable OR NOT _args)
        return()
    endif()

    get_filename_component(_executable_name "${_executable}" NAME_WE)
    if(NOT _executable_name STREQUAL "zig")
        return()
    endif()

    # `zig` is a multi-tool: only the cc/c++ subcommands make it a C/C++ compiler.
    list(GET _args 0 _subcommand)
    if(NOT _subcommand MATCHES "^(cc|c\\+\\+)$")
        return()
    endif()

    set(${ZIG_EXECUTABLE} "${_executable}" PARENT_SCOPE)
    set(${ZIG_ARGS} "${_args}" PARENT_SCOPE)
endfunction()


# zig builds against its own bundled libc/libc++ headers, so the resulting binaries
# differ from those of a plain clang of the same version. os.ag_cc_is_zig feeds the
# package hash to keep the two from sharing a package ID.
function(detect_cc_is_zig CC_IS_ZIG)
    is_zig_compiler(C _is_zig)
    if(_is_zig)
        message(STATUS "CMake-Conan: [settings] os.ag_cc_is_zig=1")
        set(${CC_IS_ZIG} "1" PARENT_SCOPE)
    else()
        set(${CC_IS_ZIG} "" PARENT_SCOPE)
    endif()
endfunction()


# Pull the target triple out of `-target <triple>` / `--target=<triple>`; empty if
# zig was given no explicit target (i.e. it builds for the host).
function(zig_target_triple ZIG_ARGS TRIPLE)
    set(${TRIPLE} "" PARENT_SCOPE)
    set(_expect_triple FALSE)
    foreach(_arg IN LISTS ZIG_ARGS)
        if(_expect_triple)
            set(${TRIPLE} "${_arg}" PARENT_SCOPE)
            return()
        endif()
        if(_arg STREQUAL "-target" OR _arg STREQUAL "--target")
            set(_expect_triple TRUE)
        elseif(_arg MATCHES "^--?target=(.+)$")
            set(${TRIPLE} "${CMAKE_MATCH_1}" PARENT_SCOPE)
            return()
        endif()
    endforeach()
endfunction()


# Conan's compiler_executables conf takes a single executable with no arguments, so
# `zig cc -target ...` cannot be passed through directly. Generate a wrapper script
# that supplies the subcommand and target, and hand Conan that instead.
function(write_zig_wrapper LANG ZIG_EXECUTABLE ZIG_ARGS WRAPPER)
    set(_wrapper_dir "${CMAKE_BINARY_DIR}/zig-wrappers")
    if(LANG STREQUAL "C")
        set(_suffix "cc")
    else()
        set(_suffix "c++")
    endif()

    # Name the wrapper `<triple>-cc` after the usual cross-toolchain convention, so
    # build systems that infer cross-compilation from the compiler name recognise it.
    # Reject anything that isn't triple-shaped rather than build a path out of it.
    zig_target_triple("${ZIG_ARGS}" _triple)
    if(_triple MATCHES "^[A-Za-z0-9][A-Za-z0-9_.+-]*$")
        set(_wrapper_name "${_triple}-${_suffix}")
    else()
        if(_triple)
            message(WARNING "CMake-Conan: Ignoring unusable zig target triple '${_triple}' "
                            "for the ${LANG} wrapper name.")
        endif()
        set(_wrapper_name "zig-${_suffix}")
    endif()

    # Quote every argument so targets/flags survive the shell verbatim.
    set(_quoted_args "")
    foreach(_arg IN LISTS ZIG_ARGS)
        string(APPEND _quoted_args " \"${_arg}\"")
    endforeach()

    if(CMAKE_HOST_WIN32)
        set(_wrapper "${_wrapper_dir}/${_wrapper_name}.cmd")
        set(_content "@echo off\r\n\"${ZIG_EXECUTABLE}\"${_quoted_args} %*\r\n")
    else()
        set(_wrapper "${_wrapper_dir}/${_wrapper_name}")
        set(_content "#!/bin/sh\nexec \"${ZIG_EXECUTABLE}\"${_quoted_args} \"$@\"\n")
    endif()

    file(WRITE "${_wrapper}" "${_content}")
    file(CHMOD "${_wrapper}" PERMISSIONS
         OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
    message(STATUS "CMake-Conan: Created zig wrapper for ${LANG}: ${_wrapper}")
    set(${WRAPPER} "${_wrapper}" PARENT_SCOPE)
endfunction()


# Replace `zig cc -target ...` in CMAKE_<LANG>_COMPILER with the wrapper script, so
# the compiler is a plain single executable for CMake, Conan and everything that
# prepends to the compiler command. In particular CMAKE_<LANG>_COMPILER_LAUNCHER:
# sccache/ccache probe the first word of the command (`zig -E ...`), which zig
# rejects as an unknown command, and the launcher gives up with "compiler not
# supported". Must run before the language is enabled, i.e. before project().
function(substitute_zig_compiler LANG)
    detect_zig_compiler(${LANG} _zig_executable _zig_args)

    if(NOT _zig_executable)
        # Compiler is no longer zig (or already substituted): drop a stale record.
        if(_CONAN_ZIG_${LANG}_WRAPPER AND NOT CMAKE_${LANG}_COMPILER STREQUAL "${_CONAN_ZIG_${LANG}_WRAPPER}")
            unset(_CONAN_ZIG_${LANG}_WRAPPER CACHE)
            unset(_CONAN_ZIG_${LANG}_COMMAND CACHE)
        endif()
        return()
    endif()

    # On Windows the wrapper is a .cmd, which CMake cannot drive as the compiler.
    # Leave the command alone there; Conan still gets the wrapper.
    if(CMAKE_HOST_WIN32)
        return()
    endif()

    write_zig_wrapper(${LANG} "${_zig_executable}" "${_zig_args}" _wrapper)
    # Keep the original command around: once substituted, nothing downstream can
    # tell that this is zig, and the Conan settings depend on it.
    set(_CONAN_ZIG_${LANG}_COMMAND "${CMAKE_${LANG}_COMPILER}" CACHE INTERNAL
        "${LANG} compiler command before the zig wrapper substitution")
    set(_CONAN_ZIG_${LANG}_WRAPPER "${_wrapper}" CACHE INTERNAL
        "zig wrapper substituted for the ${LANG} compiler")
    set(CMAKE_${LANG}_COMPILER "${_wrapper}" CACHE FILEPATH "${LANG} compiler" FORCE)
    message(STATUS "CMake-Conan: Using zig wrapper as the ${LANG} compiler: ${_wrapper}")
endfunction()


# True when LANG is compiled by zig, whether or not CMAKE_<LANG>_COMPILER has
# already been replaced by the wrapper.
function(is_zig_compiler LANG RESULT)
    detect_zig_compiler(${LANG} _zig_executable _zig_args)
    if(_zig_executable OR _CONAN_ZIG_${LANG}_WRAPPER)
        set(${RESULT} TRUE PARENT_SCOPE)
    else()
        set(${RESULT} FALSE PARENT_SCOPE)
    endif()
endfunction()


function(detect_lib_cxx LIB_CXX)
    # zig bundles its own libc++ and selects it per target; pinning compiler.libcxx
    # here would only add a setting Conan cannot honour through the wrapper.
    is_zig_compiler(CXX _is_zig)
    if(_is_zig)
        message(STATUS "CMake-Conan: zig C++ compiler detected, omitting compiler.libcxx")
        return()
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "Android")
        message(STATUS "CMake-Conan: android_stl=${CMAKE_ANDROID_STL_TYPE}")
        set(${LIB_CXX} ${CMAKE_ANDROID_STL_TYPE} PARENT_SCOPE)
        return()
    endif()

    include(CheckCXXSourceCompiles)

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
        detect_gnu_libstdcxx()
        set(${LIB_CXX} "libstdc++${_CONAN_GNU_LIBSTDCXX_SUFFIX}" PARENT_SCOPE)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "AppleClang")
        set(${LIB_CXX} "libc++" PARENT_SCOPE)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND NOT CMAKE_SYSTEM_NAME MATCHES "Windows")
        # Check for libc++
        detect_libcxx()
        if(_CONAN_IS_LIBCXX)
            set(${LIB_CXX} "libc++" PARENT_SCOPE)
            return()
        endif()

        # Check for libstdc++
        detect_gnu_libstdcxx()
        if(_CONAN_IS_GNU_LIBSTDCXX)
            set(${LIB_CXX} "libstdc++${_CONAN_GNU_LIBSTDCXX_SUFFIX}" PARENT_SCOPE)
            return()
        endif()

        # TODO: it would be an error if we reach this point
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
        # Do nothing - compiler.runtime and compiler.runtime_type
        # should be handled separately: https://github.com/conan-io/cmake-conan/pull/516
        return()
    else()
        # TODO: unable to determine, ask user to provide a full profile file instead
    endif()
endfunction()


function(detect_compiler COMPILER COMPILER_VERSION COMPILER_RUNTIME COMPILER_RUNTIME_TYPE)
    if(DEFINED CMAKE_CXX_COMPILER_ID)
        set(_COMPILER ${CMAKE_CXX_COMPILER_ID})
        set(_COMPILER_VERSION ${CMAKE_CXX_COMPILER_VERSION})
    else()
        if(NOT DEFINED CMAKE_C_COMPILER_ID)
            message(FATAL_ERROR "C or C++ compiler not defined")
        endif()
        set(_COMPILER ${CMAKE_C_COMPILER_ID})
        set(_COMPILER_VERSION ${CMAKE_C_COMPILER_VERSION})
    endif()

    message(STATUS "CMake-Conan: CMake compiler=${_COMPILER}")
    message(STATUS "CMake-Conan: CMake compiler version=${_COMPILER_VERSION}")

    if(_COMPILER MATCHES MSVC)
        set(_COMPILER "msvc")
        string(SUBSTRING ${MSVC_VERSION} 0 3 _COMPILER_VERSION)
        # Configure compiler.runtime and compiler.runtime_type settings for MSVC
        if(CMAKE_MSVC_RUNTIME_LIBRARY)
            set(_KNOWN_MSVC_RUNTIME_VALUES "")
            list(APPEND _KNOWN_MSVC_RUNTIME_VALUES MultiThreaded MultiThreadedDLL)
            list(APPEND _KNOWN_MSVC_RUNTIME_VALUES MultiThreadedDebug MultiThreadedDebugDLL)
            list(APPEND _KNOWN_MSVC_RUNTIME_VALUES MultiThreaded$<$<CONFIG:Debug>:Debug> MultiThreaded$<$<CONFIG:Debug>:Debug>DLL)

            # only accept the 6 possible values, otherwise we don't don't know to map this
            if(NOT CMAKE_MSVC_RUNTIME_LIBRARY IN_LIST _KNOWN_MSVC_RUNTIME_VALUES)
                message(FATAL_ERROR "CMake-Conan: unable to map MSVC runtime: ${CMAKE_MSVC_RUNTIME_LIBRARY} to Conan settings")
            endif()

            # Runtime is "dynamic" in all cases if it ends in DLL
            if(CMAKE_MSVC_RUNTIME_LIBRARY MATCHES ".*DLL$")
                set(_COMPILER_RUNTIME "dynamic")
            else()
                set(_COMPILER_RUNTIME "static")
            endif()

            # Only define compiler.runtime_type when explicitly requested
            # If a generator expression is used, let Conan handle it conditional on build_type
            get_property(_IS_MULTI_CONFIG_GENERATOR GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
            if(NOT CMAKE_MSVC_RUNTIME_LIBRARY MATCHES "<CONFIG:Debug>:Debug>")
                if(CMAKE_MSVC_RUNTIME_LIBRARY MATCHES "Debug")
                    set(_COMPILER_RUNTIME_TYPE "Debug")
                else()
                    set(_COMPILER_RUNTIME_TYPE "Release")
                endif()
            endif()

            unset(_KNOWN_MSVC_RUNTIME_VALUES)
            unset(_IS_MULTI_CONFIG_GENERATOR)
        endif()
    elseif(_COMPILER MATCHES AppleClang)
        set(_COMPILER "apple-clang")
        string(REPLACE "." ";" VERSION_LIST ${CMAKE_CXX_COMPILER_VERSION})
        list(GET VERSION_LIST 0 _COMPILER_VERSION)
    elseif(_COMPILER MATCHES Clang)
        set(_COMPILER "clang")
        string(REPLACE "." ";" VERSION_LIST ${CMAKE_CXX_COMPILER_VERSION})
        list(GET VERSION_LIST 0 _COMPILER_VERSION)
    elseif(_COMPILER MATCHES GNU)
        set(_COMPILER "gcc")
        string(REPLACE "." ";" VERSION_LIST ${CMAKE_CXX_COMPILER_VERSION})
        list(GET VERSION_LIST 0 _COMPILER_VERSION)
    endif()

    message(STATUS "CMake-Conan: [settings] compiler=${_COMPILER}")
    message(STATUS "CMake-Conan: [settings] compiler.version=${_COMPILER_VERSION}")
    if (_COMPILER_RUNTIME)
        message(STATUS "CMake-Conan: [settings] compiler.runtime=${_COMPILER_RUNTIME}")
    endif()
    if (_COMPILER_RUNTIME_TYPE)
        message(STATUS "CMake-Conan: [settings] compiler.runtime_type=${_COMPILER_RUNTIME_TYPE}")
    endif()

    set(${COMPILER} ${_COMPILER} PARENT_SCOPE)
    set(${COMPILER_VERSION} ${_COMPILER_VERSION} PARENT_SCOPE)
    set(${COMPILER_RUNTIME} ${_COMPILER_RUNTIME} PARENT_SCOPE)
    set(${COMPILER_RUNTIME_TYPE} ${_COMPILER_RUNTIME_TYPE} PARENT_SCOPE)
endfunction()


function(detect_build_type BUILD_TYPE)
    get_property(_MULTICONFIG_GENERATOR GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
    if(NOT _MULTICONFIG_GENERATOR)
        # Only set when we know we are in a single-configuration generator
        # Note: we may want to fail early if `CMAKE_BUILD_TYPE` is not defined
        set(${BUILD_TYPE} ${CMAKE_BUILD_TYPE} PARENT_SCOPE)
    endif()
endfunction()


# Resolve the executable to hand to Conan for LANG, substituting a generated
# wrapper script when the compiler is `zig cc`/`zig c++`.
function(conan_compiler_executable LANG EXECUTABLE)
    detect_zig_compiler(${LANG} _zig_executable _zig_args)
    if(_zig_executable)
        write_zig_wrapper(${LANG} "${_zig_executable}" "${_zig_args}" _wrapper)
        set(${EXECUTABLE} "${_wrapper}" PARENT_SCOPE)
    else()
        set(${EXECUTABLE} "${CMAKE_${LANG}_COMPILER}" PARENT_SCOPE)
    endif()
endfunction()


macro(append_compiler_executables_configuration)
    set(_conan_c_compiler "")
    set(_conan_cpp_compiler "")
    if(CMAKE_C_COMPILER)
        conan_compiler_executable(C _c_executable)
        set(_conan_c_compiler "\"c\":\"${_c_executable}\",")
    else()
        message(WARNING "CMake-Conan: The C compiler is not defined. "
                        "Please define CMAKE_C_COMPILER or enable the C language.")
    endif()
    if(CMAKE_CXX_COMPILER)
        conan_compiler_executable(CXX _cpp_executable)
        set(_conan_cpp_compiler "\"cpp\":\"${_cpp_executable}\"")
    else()
        message(WARNING "CMake-Conan: The C++ compiler is not defined. "
                        "Please define CMAKE_CXX_COMPILER or enable the C++ language.")
    endif()

    string(APPEND PROFILE "tools.build:compiler_executables={${_conan_c_compiler}${_conan_cpp_compiler}}\n")
    unset(_conan_c_compiler)
    unset(_conan_cpp_compiler)
    unset(_c_executable)
    unset(_cpp_executable)
endmacro()


function(detect_host_profile output_file)
    detect_os(MYOS MYOS_API_LEVEL MYOS_SDK MYOS_SUBSYSTEM MYOS_VERSION)
    detect_arch(MYARCH)
    detect_compiler(MYCOMPILER MYCOMPILER_VERSION MYCOMPILER_RUNTIME MYCOMPILER_RUNTIME_TYPE)
    detect_cxx_standard(MYCXX_STANDARD)
    detect_lib_cxx(MYLIB_CXX)
    detect_build_type(MYBUILD_TYPE)
    detect_cc_is_zig(MYCC_IS_ZIG)

    set(PROFILE "")
    string(APPEND PROFILE "[settings]\n")
    if(MYARCH)
        string(APPEND PROFILE arch=${MYARCH} "\n")
    endif()
    if(MYOS)
        string(APPEND PROFILE os=${MYOS} "\n")
    endif()
    if(MYOS_API_LEVEL)
        string(APPEND PROFILE os.api_level=${MYOS_API_LEVEL} "\n")
    endif()
    if(MYOS_VERSION)
        string(APPEND PROFILE os.version=${MYOS_VERSION} "\n")
    endif()
    if(MYOS_SDK)
        string(APPEND PROFILE os.sdk=${MYOS_SDK} "\n")
    endif()
    if(MYOS_SUBSYSTEM)
        string(APPEND PROFILE os.subsystem=${MYOS_SUBSYSTEM} "\n")
    endif()
    # os.ag_cc_is_zig is only defined as a sub-setting of os, so it needs os to be set.
    if(MYOS AND MYCC_IS_ZIG)
        string(APPEND PROFILE os.ag_cc_is_zig=${MYCC_IS_ZIG} "\n")
    endif()
    if(MYCOMPILER)
        string(APPEND PROFILE compiler=${MYCOMPILER} "\n")
    endif()
    if(MYCOMPILER_VERSION)
        string(APPEND PROFILE compiler.version=${MYCOMPILER_VERSION} "\n")
    endif()
    if(MYCOMPILER_RUNTIME)
        string(APPEND PROFILE compiler.runtime=${MYCOMPILER_RUNTIME} "\n")
    endif()
    if(MYCOMPILER_RUNTIME_TYPE)
        string(APPEND PROFILE compiler.runtime_type=${MYCOMPILER_RUNTIME_TYPE} "\n")
    endif()
    if(MYCXX_STANDARD)
        string(APPEND PROFILE compiler.cppstd=${MYCXX_STANDARD} "\n")
    endif()
    if(MYLIB_CXX)
        string(APPEND PROFILE compiler.libcxx=${MYLIB_CXX} "\n")
    endif()
    if(MYBUILD_TYPE)
        string(APPEND PROFILE "build_type=${MYBUILD_TYPE}\n")
    endif()

    if(NOT DEFINED output_file)
        set(_FN "${CMAKE_BINARY_DIR}/profile")
    else()
        set(_FN ${output_file})
    endif()

    string(APPEND PROFILE "[conf]\n")
    string(APPEND PROFILE "tools.cmake.cmaketoolchain:generator=${CMAKE_GENERATOR}\n")

    # propagate compilers via profile
    append_compiler_executables_configuration()

    if(MYOS STREQUAL "Android")
        string(APPEND PROFILE "tools.android:ndk_path=${CMAKE_ANDROID_NDK}\n")
    endif()

    message(STATUS "CMake-Conan: Creating profile ${_FN}")
    file(WRITE ${_FN} ${PROFILE})
    message(STATUS "CMake-Conan: Profile: \n${PROFILE}")
endfunction()


function(conan_profile_detect_default)
    message(STATUS "CMake-Conan: Checking if a default profile exists")
    execute_process(COMMAND ${CONAN_COMMAND} profile path default
                    RESULT_VARIABLE return_code
                    OUTPUT_VARIABLE conan_stdout
                    ERROR_VARIABLE conan_stderr
                    ECHO_ERROR_VARIABLE    # show the text output regardless
                    ECHO_OUTPUT_VARIABLE
                    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    if(NOT ${return_code} EQUAL "0")
        message(STATUS "CMake-Conan: The default profile doesn't exist, detecting it.")
        execute_process(COMMAND ${CONAN_COMMAND} profile detect
            RESULT_VARIABLE return_code
            OUTPUT_VARIABLE conan_stdout
            ERROR_VARIABLE conan_stderr
            ECHO_ERROR_VARIABLE    # show the text output regardless
            ECHO_OUTPUT_VARIABLE
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    endif()
endfunction()


function(conan_install)
    cmake_parse_arguments(ARGS CONAN_ARGS ${ARGN})
    set(CONAN_OUTPUT_FOLDER ${CMAKE_BINARY_DIR}/conan)
    # Invoke "conan install" with the provided arguments
    set(CONAN_ARGS ${CONAN_ARGS} -of=${CONAN_OUTPUT_FOLDER})
    message(STATUS "CMake-Conan: conan install ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/.. ${CONAN_ARGS} ${ARGN}")
    execute_process(COMMAND ${CONAN_COMMAND} install ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/.. ${CONAN_ARGS} ${ARGN} --format=json
                    RESULT_VARIABLE return_code
                    OUTPUT_VARIABLE conan_stdout
                    ERROR_VARIABLE conan_stderr
                    ECHO_ERROR_VARIABLE    # show the text output regardless
                    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    if(NOT "${return_code}" STREQUAL "0")
        message(FATAL_ERROR "Conan install failed='${return_code}'")
    else()
        # the files are generated in a folder that depends on the layout used, if
        # one is specified, but we don't know a priori where this is.
        # TODO: this can be made more robust if Conan can provide this in the json output
        string(JSON CONAN_GENERATORS_FOLDER GET ${conan_stdout} graph nodes 0 generators_folder)
        cmake_path(CONVERT ${CONAN_GENERATORS_FOLDER} TO_CMAKE_PATH_LIST CONAN_GENERATORS_FOLDER)
        # message("conan stdout: ${conan_stdout}")
        message(STATUS "CMake-Conan: CONAN_GENERATORS_FOLDER=${CONAN_GENERATORS_FOLDER}")
        set_property(GLOBAL PROPERTY CONAN_GENERATORS_FOLDER "${CONAN_GENERATORS_FOLDER}")
        # reconfigure on conanfile changes
        string(JSON CONANFILE GET ${conan_stdout} graph nodes 0 label)
        message(STATUS "CMake-Conan: CONANFILE=${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../${CONANFILE}")
        set_property(DIRECTORY ${CMAKE_SOURCE_DIR} APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../${CONANFILE}")
        # success
        set_property(GLOBAL PROPERTY CONAN_INSTALL_SUCCESS TRUE)
    endif()
endfunction()


function(conan_config_install)
    find_program(CONAN_COMMAND "conan" REQUIRED)
    # Invoke "conan config install" with the provided arguments
    message(STATUS "CMake-Conan: conan config install ${ARGN}")
    execute_process(COMMAND ${CONAN_COMMAND} config install ${ARGN}
            RESULT_VARIABLE return_code
            OUTPUT_VARIABLE conan_stdout
            ERROR_VARIABLE conan_stderr
            ECHO_ERROR_VARIABLE    # show the text output regardless
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    if(NOT "${return_code}" STREQUAL "0")
        message(FATAL_ERROR "Conan config install failed='${return_code}'")
    endif()
endfunction()


function(conan_get_version conan_command conan_current_version)
    execute_process(
        COMMAND ${conan_command} --version
        OUTPUT_VARIABLE conan_output
        RESULT_VARIABLE conan_result
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(conan_result)
        message(FATAL_ERROR "CMake-Conan: Error when trying to run Conan")
    endif()

    string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" conan_version ${conan_output})
    set(${conan_current_version} ${conan_version} PARENT_SCOPE)
endfunction()


function(conan_version_check)
    set(options )
    set(oneValueArgs MINIMUM CURRENT)
    set(multiValueArgs )
    cmake_parse_arguments(CONAN_VERSION_CHECK
        "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT CONAN_VERSION_CHECK_MINIMUM)
        message(FATAL_ERROR "CMake-Conan: Required parameter MINIMUM not set!")
    endif()
        if(NOT CONAN_VERSION_CHECK_CURRENT)
        message(FATAL_ERROR "CMake-Conan: Required parameter CURRENT not set!")
    endif()

    if(CONAN_VERSION_CHECK_CURRENT VERSION_LESS CONAN_VERSION_CHECK_MINIMUM)
        message(FATAL_ERROR "CMake-Conan: Conan version must be ${CONAN_VERSION_CHECK_MINIMUM} or later")
    endif()
endfunction()


macro(construct_profile_argument argument_variable profile_list)
    set(${argument_variable} "")
    if("${profile_list}" STREQUAL "CONAN_HOST_PROFILE")
        set(_arg_flag "--profile:host=")
    elseif("${profile_list}" STREQUAL "CONAN_BUILD_PROFILE")
        set(_arg_flag "--profile:build=")
    endif()

    set(_profile_list "${${profile_list}}")
    list(TRANSFORM _profile_list REPLACE "auto-cmake" "${CMAKE_BINARY_DIR}/conan_host_profile")
    list(TRANSFORM _profile_list PREPEND ${_arg_flag})
    set(${argument_variable} ${_profile_list})

    unset(_arg_flag)
    unset(_profile_list)
endmacro()


macro(conan_provide_dependency method package_name)
    set_property(GLOBAL PROPERTY CONAN_PROVIDE_DEPENDENCY_INVOKED TRUE)
    get_property(_conan_install_success GLOBAL PROPERTY CONAN_INSTALL_SUCCESS)
    if(NOT _conan_install_success)
        find_program(CONAN_COMMAND "conan" REQUIRED)
        conan_get_version(${CONAN_COMMAND} CONAN_CURRENT_VERSION)
        conan_version_check(MINIMUM ${CONAN_MINIMUM_VERSION} CURRENT ${CONAN_CURRENT_VERSION})
        message(STATUS "CMake-Conan: first find_package() found. Installing dependencies with Conan")
        if("default" IN_LIST CONAN_HOST_PROFILE OR "default" IN_LIST CONAN_BUILD_PROFILE)
            conan_profile_detect_default()
        endif()
        if("auto-cmake" IN_LIST CONAN_HOST_PROFILE)
            detect_host_profile(${CMAKE_BINARY_DIR}/conan_host_profile)
        endif()
        construct_profile_argument(_host_profile_flags CONAN_HOST_PROFILE)
        construct_profile_argument(_build_profile_flags CONAN_BUILD_PROFILE)
        if(EXISTS "${CMAKE_SOURCE_DIR}/conanfile.py")
            file(READ "${CMAKE_SOURCE_DIR}/conanfile.py" outfile)
            if(NOT "${outfile}" MATCHES ".*CMakeDeps.*")
                message(WARNING "Cmake-conan: CMakeDeps generator was not defined in the conanfile")
            endif()
            set(generator "")
        elseif (EXISTS "${CMAKE_SOURCE_DIR}/conanfile.txt")
            file(READ "${CMAKE_SOURCE_DIR}/conanfile.txt" outfile)
            if(NOT "${outfile}" MATCHES ".*CMakeDeps.*")
                message(WARNING "Cmake-conan: CMakeDeps generator was not defined in the conanfile. "
                        "Please define the generator as it will be mandatory in the future")
            endif()
            set(generator "-g;CMakeDeps")
        endif()
        get_property(_multiconfig_generator GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
        if(NOT _multiconfig_generator)
            message(STATUS "CMake-Conan: Installing single configuration ${CMAKE_BUILD_TYPE}")
            conan_install(${_host_profile_flags} ${_build_profile_flags} --build=missing ${generator})
        else()
            message(STATUS "CMake-Conan: Installing both Debug and Release")
            conan_install(${_host_profile_flags} ${_build_profile_flags} -s build_type=Release --build=missing ${generator})
            conan_install(${_host_profile_flags} ${_build_profile_flags} -s build_type=Debug --build=missing ${generator})
        endif()
        unset(_host_profile_flags)
        unset(_build_profile_flags)
        unset(_multiconfig_generator)
        unset(_conan_install_success)
    else()
        message(STATUS "CMake-Conan: find_package(${ARGV1}) found, 'conan install' already ran")
        unset(_conan_install_success)
    endif()

    get_property(_conan_generators_folder GLOBAL PROPERTY CONAN_GENERATORS_FOLDER)

    # Ensure that we consider Conan-provided packages ahead of any other,
    # irrespective of other settings that modify the search order or search paths
    # This follows the guidelines from the find_package documentation
    #  (https://cmake.org/cmake/help/latest/command/find_package.html):
    #       find_package (<PackageName> PATHS paths... NO_DEFAULT_PATH)
    #       find_package (<PackageName>)

    # Filter out `REQUIRED` from the argument list, as the first call may fail
    set(_find_args_${package_name} "${ARGN}")
    list(REMOVE_ITEM _find_args_${package_name} "REQUIRED")
    if(NOT "MODULE" IN_LIST _find_args_${package_name})
        find_package(${package_name} ${_find_args_${package_name}} BYPASS_PROVIDER PATHS "${_conan_generators_folder}" NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
        unset(_find_args_${package_name})
    endif()

    # Invoke find_package a second time - if the first call succeeded,
    # this will simply reuse the result. If not, fall back to CMake default search
    # behaviour, also allowing modules to be searched.
    if(NOT ${package_name}_FOUND)
        list(FIND CMAKE_MODULE_PATH "${_conan_generators_folder}" _index)
        if(_index EQUAL -1)
            list(PREPEND CMAKE_MODULE_PATH "${_conan_generators_folder}")
        endif()
        unset(_index)
        find_package(${package_name} ${ARGN} BYPASS_PROVIDER)
        list(REMOVE_ITEM CMAKE_MODULE_PATH "${_conan_generators_folder}")
    endif()
endmacro()


cmake_language(
    SET_DEPENDENCY_PROVIDER conan_provide_dependency
    SUPPORTED_METHODS FIND_PACKAGE
)


macro(conan_provide_dependency_check)
    set(_CONAN_PROVIDE_DEPENDENCY_INVOKED FALSE)
    get_property(_CONAN_PROVIDE_DEPENDENCY_INVOKED GLOBAL PROPERTY CONAN_PROVIDE_DEPENDENCY_INVOKED)
    if(NOT _CONAN_PROVIDE_DEPENDENCY_INVOKED)
        message(WARNING "Conan is correctly configured as dependency provider, "
                        "but Conan has not been invoked. Please add at least one "
                        "call to `find_package()`.")
        if(DEFINED CONAN_COMMAND)
            # supress warning in case `CONAN_COMMAND` was specified but unused.
            set(_CONAN_COMMAND ${CONAN_COMMAND})
            unset(_CONAN_COMMAND)
        endif()
    endif()
    unset(_CONAN_PROVIDE_DEPENDENCY_INVOKED)
endmacro()


# Add a deferred call at the end of processing the top-level directory
# to check if the dependency provider was invoked at all.
cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL conan_provide_dependency_check)

# This file is pulled in through CMAKE_PROJECT_TOP_LEVEL_INCLUDES, i.e. from
# project() but before any language is enabled, which is the only point where
# CMAKE_<LANG>_COMPILER can still be rewritten without upsetting CMake.
substitute_zig_compiler(C)
substitute_zig_compiler(CXX)

# Profile selection must see the compiler command as the user gave it, not the
# substituted wrapper, so that it does not depend on which configure run this is.
set(_C_COMPILER_COMMAND "${CMAKE_C_COMPILER}")
if(_CONAN_ZIG_C_COMMAND)
    set(_C_COMPILER_COMMAND "${_CONAN_ZIG_C_COMMAND}")
endif()

# Detect conan profile to use
set(_PROFILES_DIR ${CMAKE_CURRENT_LIST_DIR}/../conan/profiles)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    if (_C_COMPILER_COMMAND MATCHES ".*-musl(eabi)?-.*")
        if(SANITIZE)
            set(_SELECTED_PROFILE "${_PROFILES_DIR}/linux-musl-asan.jinja")
        else()
            set(_SELECTED_PROFILE "${_PROFILES_DIR}/linux-musl.jinja")
        endif()
    else()
        if(SANITIZE)
            set(_SELECTED_PROFILE "${_PROFILES_DIR}/linux-asan.jinja")
        else()
            set(_SELECTED_PROFILE "${_PROFILES_DIR}/linux.jinja")
        endif()
    endif()
elseif (APPLE)
    if(SANITIZE)
        set(_SELECTED_PROFILE "${_PROFILES_DIR}/apple-asan.jinja")
    else()
        set(_SELECTED_PROFILE "${_PROFILES_DIR}/apple.jinja")
    endif()
elseif(ANDROID)
    set(_SELECTED_PROFILE "${_PROFILES_DIR}/android.jinja")
elseif(WIN32)
    set(_SELECTED_PROFILE "${_PROFILES_DIR}/windows-msvc.jinja")
else()
    set(_SELECTED_PROFILE "default")
endif()
# Install custom config
conan_config_install(${CMAKE_CURRENT_LIST_DIR}/../conan/settings_user.yml)

# Configurable variables for Conan profiles
set(CONAN_HOST_PROFILE "${_SELECTED_PROFILE};auto-cmake" CACHE STRING "Conan host profile")
set(CONAN_BUILD_PROFILE "default" CACHE STRING "Conan build profile")
