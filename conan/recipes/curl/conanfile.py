from conans import ConanFile, CMake, tools


class CurlConan(ConanFile):
    name = "libcurl"
    version = "7.78.0"
    settings = "os", "compiler", "build_type", "arch"
    options = {"shared": [True, False], "fPIC": [True, False]}
    default_options = {"shared": False, "fPIC": True, "libnghttp2:with_app": False, "libnghttp2:with_hpack": False}
    generators = "cmake"
    requires = "openssl/boring-2021-05-11@AdguardTeam/NativeLibsCommon", \
               "nghttp2/1.44.0-2021-09-14@AdguardTeam/NativeLibsCommon"
    exports_sources = ["CMakeLists.txt", "patches/*"]

    def config_options(self):
        if self.settings.os == "Windows":
            del self.options.fPIC

    def source(self):
        self.run("git clone https://github.com/curl/curl source_subfolder")
        self.run("cd source_subfolder && git checkout curl-7_78_0")
        tools.patch(base_path="source_subfolder", patch_file="patches/nghttp2_cmake.patch")

    def build(self):
        cmake = CMake(self)
        cmake.definitions["BUILD_CURL_EXE"] = "OFF";
        cmake.definitions["BUILD_SHARED_LIBS"] = "OFF";
        cmake.definitions["CURL_DISABLE_COOKIES"] = "ON";
        cmake.definitions["CURL_STATICLIB"] = "ON";
        cmake.definitions["CMAKE_USE_LIBSSH2"] = "OFF";
        cmake.definitions["CMAKE_USE_SCHANNEL"] = "OFF";
        cmake.definitions["CMAKE_USE_SECTRANSP"] = "OFF";
        cmake.definitions["CMAKE_USE_OPENSSL"] = "ON";
        cmake.definitions["CMAKE_USE_MBEDTLS"] = "OFF";
        cmake.definitions["CMAKE_USE_BEARSSL"] = "OFF";
        cmake.definitions["CMAKE_USE_NSS"] = "OFF";
        cmake.definitions["CMAKE_USE_WOLFSSL"] = "OFF";
        cmake.definitions["ENABLE_MANUAL"] = "OFF";
        cmake.definitions["HTTP_ONLY"] = "ON";
        cmake.definitions["USE_NGHTTP2"] = "ON";

        cmake.definitions["OPENSSL_ROOT_DIR"] = self.deps_cpp_info["openssl"].rootpath
        cmake.definitions["NGHTTP2_INCLUDE_DIR"] = self.deps_cpp_info["nghttp2"].rootpath + "/include"
        cmake.definitions["NGHTTP2_LIBRARY"] = self.deps_cpp_info["nghttp2"].rootpath + "/lib/nghttp2.*"

        cmake.configure()
        cmake.build()

        # Explicit way:
        # self.run('cmake %s/hello %s'
        #          % (self.source_folder, cmake.command_line))
        # self.run("cmake --build . %s" % cmake.build_config)

    def package(self):
        self.copy("*.h", dst="include/curl", src="source_subfolder/include/curl")
        self.copy("bin/curl", dst="bin", keep_path=False)
        self.copy("*.dll", dst="bin", keep_path=False)
        self.copy("*.lib", dst="lib", keep_path=False)
        self.copy("*.so", dst="lib", keep_path=False)
        self.copy("*.dylib", dst="lib", keep_path=False)
        self.copy("*.a", dst="lib", keep_path=False)

    def package_info(self):
        if self.settings.os == "Windows":
            self.cpp_info.system_libs = ["crypt32"]
            self.cpp_info.libs = ["libcurl"]
        elif self.settings.os == "Macos":
            self.cpp_info.system_libs = ["-Wl,-framework,SystemConfiguration"]
            self.cpp_info.libs = ["curl"]
        else:
            self.cpp_info.libs = ["curl"]

        self.cpp_info.defines.append("CURL_STATICLIB=1")
