from conans import ConanFile, CMake


class NGHttp3Conan(ConanFile):
    name = "nghttp3"
    version = "0.7.1"
    settings = "os", "compiler", "build_type", "arch"
    options = {}
    default_options = {}
    generators = "cmake"
    exports_sources = ["CMakeLists.txt"]

    def source(self):
        self.run("git clone https://github.com/ngtcp2/nghttp3.git source_subfolder")
        self.run("cd source_subfolder && git checkout v0.7.1")

    def build(self):
        cmake = CMake(self)
        cmake.definitions["ENABLE_LIB_ONLY"] = "ON"
        cmake.definitions["ENABLE_STATIC_LIB"] = "ON"
        cmake.definitions["ENABLE_SHARED_LIB"] = "OFF"
        cmake.configure()
        cmake.build()

    def package(self):
        self.copy("*.h", dst="include/nghttp3", src="source_subfolder/lib/includes/nghttp3")
        self.copy("*.lib", dst="lib", keep_path=False)
        self.copy("*.a", dst="lib", keep_path=False)

    def package_info(self):
        self.cpp_info.libs = ["nghttp3"]
        self.cpp_info.defines.append("NGHTTP3_STATICLIB")
