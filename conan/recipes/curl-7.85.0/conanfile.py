from conans import ConanFile, CMake, tools


class CurlConan(ConanFile):
    name = "libcurl"
    version = "7.85.0-adguard5"
    settings = "os", "compiler", "build_type", "arch"
    options = {"shared": [True, False], "fPIC": [True, False]}
    default_options = {"shared": False, "fPIC": True, "libnghttp2:with_app": False, "libnghttp2:with_hpack": False}
    generators = "cmake"
    requires = "openssl/boring-2021-05-11@AdguardTeam/NativeLibsCommon", \
               "nghttp2/1.44.0@AdguardTeam/NativeLibsCommon", \
               "nghttp3/0.7.1@AdguardTeam/NativeLibsCommon", \
               "ngtcp2/0.9.0@AdguardTeam/NativeLibsCommon", \
               "zlib/1.2.11"
    exports_sources = ["CMakeLists.txt", "patches/*"]

    def config_options(self):
        if self.settings.os == "Windows":
            del self.options.fPIC

    def source(self):
        self.run("git clone https://github.com/curl/curl source_subfolder")
        self.run("cd source_subfolder && git checkout curl-7_85_0")
        tools.patch(base_path="source_subfolder", patch_file="patches/00-cmake.patch")
        tools.patch(base_path="source_subfolder", patch_file="patches/01-fix-quic-conn-reuse.patch")
        tools.patch(base_path="source_subfolder", patch_file="patches/02-nghttp3-cb-nullcheck.patch")
        tools.patch(base_path="source_subfolder", patch_file="patches/03-quic-ssl-ctx-function.patch")

    def build(self):
        cmake = CMake(self)
        cmake.definitions["BUILD_CURL_EXE"] = "OFF"
        cmake.definitions["BUILD_SHARED_LIBS"] = "OFF"
        cmake.definitions["CURL_DISABLE_COOKIES"] = "ON"
        cmake.definitions["CURL_STATICLIB"] = "ON"
        cmake.definitions["CURL_USE_LIBSSH2"] = "OFF"
        cmake.definitions["CURL_USE_SCHANNEL"] = "OFF"
        cmake.definitions["CURL_USE_SECTRANSP"] = "OFF"
        cmake.definitions["CURL_USE_OPENSSL"] = "ON"
        cmake.definitions["CURL_USE_MBEDTLS"] = "OFF"
        cmake.definitions["CURL_USE_BEARSSL"] = "OFF"
        cmake.definitions["CURL_USE_NSS"] = "OFF"
        cmake.definitions["CURL_USE_WOLFSSL"] = "OFF"
        cmake.definitions["ENABLE_MANUAL"] = "OFF"
        cmake.definitions["HTTP_ONLY"] = "ON"
        cmake.definitions["USE_NGHTTP2"] = "ON"
        cmake.definitions["USE_NGTCP2"] = "ON"
        cmake.definitions["USE_NGHTTP3"] = "ON"

        cmake.definitions["OPENSSL_ROOT_DIR"] = self.deps_cpp_info["openssl"].rootpath

        cmake.definitions["NGHTTP2_INCLUDE_DIR"] = self.deps_cpp_info["nghttp2"].rootpath + "/include"
        cmake.definitions["NGHTTP2_LIBRARY"] = self.deps_cpp_info["nghttp2"].rootpath + "/lib/nghttp2.*"

        cmake.definitions["NGHTTP3_INCLUDE_DIR"] = self.deps_cpp_info["nghttp3"].rootpath + "/include"
        cmake.definitions["NGHTTP3_LIBRARY"] = self.deps_cpp_info["nghttp3"].rootpath + "/lib/nghttp3.*"

        cmake.definitions["NGTCP2_INCLUDE_DIR"] = self.deps_cpp_info["ngtcp2"].rootpath + "/include"
        cmake.definitions["NGTCP2_LIBRARY"] = self.deps_cpp_info["ngtcp2"].rootpath + "/lib/ngtcp2.*"
        cmake.definitions["ngtcp2_crypto_boringssl_LIBRARY"] = \
            self.deps_cpp_info["ngtcp2"].rootpath + "/lib/ngtcp2_crypto_boringssl.*"

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
