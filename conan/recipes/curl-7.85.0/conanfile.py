from conan import ConanFile
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout
from conan.tools.files import patch


class CurlConan(ConanFile):
    name = "libcurl"
    version = "7.85.0-adguard5"
    settings = "os", "compiler", "build_type", "arch"
    options = {"shared": [True, False], "fPIC": [True, False]}
    default_options = {"shared": False, "fPIC": True, "libnghttp2:with_app": False, "libnghttp2:with_hpack": False}
    requires = "openssl/boring-2021-05-11@AdguardTeam/NativeLibsCommon", \
               "nghttp2/1.44.0@AdguardTeam/NativeLibsCommon", \
               "nghttp3/0.7.1@AdguardTeam/NativeLibsCommon", \
               "ngtcp2/0.9.0@AdguardTeam/NativeLibsCommon", \
               "zlib/1.3"
    exports_sources = ["CMakeLists.txt", "patches/*"]

    def config_options(self):
        if self.settings.os == "Windows":
            del self.options.fPIC

    def source(self):
        self.run("git clone https://github.com/curl/curl source_subfolder")
        self.run("cd source_subfolder && git checkout curl-7_85_0")
        patch(self, base_path="source_subfolder", patch_file="patches/00-cmake.patch")
        patch(self, base_path="source_subfolder", patch_file="patches/01-fix-quic-conn-reuse.patch")
        patch(self, base_path="source_subfolder", patch_file="patches/02-nghttp3-cb-nullcheck.patch")
        patch(self, base_path="source_subfolder", patch_file="patches/03-quic-ssl-ctx-function.patch")

    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()
        tc = CMakeToolchain(self)
        tc.variables["BUILD_CURL_EXE"] = "OFF"
        tc.variables["BUILD_SHARED_LIBS"] = "OFF"
        tc.variables["CURL_DISABLE_COOKIES"] = "ON"
        tc.variables["CURL_STATICLIB"] = "ON"
        tc.variables["CURL_USE_LIBSSH2"] = "OFF"
        tc.variables["CURL_USE_SCHANNEL"] = "OFF"
        tc.variables["CURL_USE_SECTRANSP"] = "OFF"
        tc.variables["CURL_USE_OPENSSL"] = "ON"
        tc.variables["CURL_USE_MBEDTLS"] = "OFF"
        tc.variables["CURL_USE_BEARSSL"] = "OFF"
        tc.variables["CURL_USE_NSS"] = "OFF"
        tc.variables["CURL_USE_WOLFSSL"] = "OFF"
        tc.variables["ENABLE_MANUAL"] = "OFF"
        tc.variables["HTTP_ONLY"] = "ON"
        tc.variables["USE_NGHTTP2"] = "ON"
        tc.variables["USE_NGTCP2"] = "ON"
        tc.variables["USE_NGHTTP3"] = "ON"

        openssl = self.dependencies["openssl"].package_folder.replace("\\", "/")
        nghttp2 = self.dependencies["nghttp2"].package_folder.replace("\\", "/")
        nghttp3 = self.dependencies["nghttp3"].package_folder.replace("\\", "/")
        ngtcp2 = self.dependencies["ngtcp2"].package_folder.replace("\\", "/")

        tc.variables["OPENSSL_INCLUDE_DIR"] = openssl + "/include"
        tc.variables["NGHTTP2_INCLUDE_DIR"] = nghttp2 + "/include"
        tc.variables["NGHTTP3_INCLUDE_DIR"] = nghttp3 + "/include"
        tc.variables["NGTCP2_INCLUDE_DIR"] = ngtcp2 + "/include"

        if self.settings.os == "Windows":
            tc.variables["NGHTTP2_LIBRARY"] = nghttp2 + "/lib/nghttp2.lib"
            tc.variables["NGHTTP3_LIBRARY"] = nghttp3 + "/lib/nghttp3.lib"
            tc.variables["NGTCP2_LIBRARY"] = ngtcp2 + "/lib/ngtcp2.lib"
            tc.variables["ngtcp2_crypto_boringssl_LIBRARY"] = \
                ngtcp2 + "/lib/ngtcp2_crypto_boringssl.lib"
            tc.variables["OPENSSL_LIBRARIES"] = (openssl + "/lib/ssl.lib" + ";" +
                                                 openssl + "/lib/crypto.lib")
        else:
            tc.variables["NGHTTP2_LIBRARY"] = nghttp2 + "/lib/libnghttp2.a"
            tc.variables["NGHTTP3_LIBRARY"] = nghttp3 + "/lib/libnghttp3.a"
            tc.variables["NGTCP2_LIBRARY"] = ngtcp2 + "/lib/libngtcp2.a"
            tc.variables["ngtcp2_crypto_boringssl_LIBRARY"] = \
                ngtcp2 + "/lib/libngtcp2_crypto_boringssl.a"
            tc.variables["OPENSSL_LIBRARIES"] = (openssl + "/lib/libssl.a" + ";" +
                                                 openssl + "/lib/libcrypto.a")

        tc.generate()

    def layout(self):
        cmake_layout(self)

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

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
