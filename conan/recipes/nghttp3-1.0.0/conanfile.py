from conans import ConanFile, CMake, tools


class NGHttp3Conan(ConanFile):
    name = "nghttp3"
    version = "1.0.0"
    settings = "os", "compiler", "build_type", "arch"
    options = {}
    default_options = {}
    generators = "cmake"
    exports_sources = ["CMakeLists.txt", "patches/popcnt_old_cpu_fix.patch"]

    def source(self):
        self.run("git clone https://github.com/ngtcp2/nghttp3.git source_subfolder")
        self.run(f"cd source_subfolder && git checkout v{self.version}")
        tools.patch(base_path="source_subfolder", patch_file="patches/popcnt_old_cpu_fix.patch")

    def build(self):
        cmake = CMake(self)
        cmake.definitions["ENABLE_LIB_ONLY"] = "ON"
        cmake.definitions["ENABLE_STATIC_LIB"] = "ON"
        cmake.definitions["ENABLE_SHARED_LIB"] = "OFF"
        if cmake.build_type == "Debug":
            cmake.definitions["DEBUGBUILD"] = "1"
        cmake.configure()
        cmake.build()

    def package(self):
        self.copy("*.h", dst="include/nghttp3", src="source_subfolder/lib/includes/nghttp3")
        self.copy("*.lib", dst="lib", keep_path=False)
        self.copy("*.a", dst="lib", keep_path=False)

    def package_info(self):
        self.cpp_info.libs = ["nghttp3"]
        self.cpp_info.defines.append("NGHTTP3_STATICLIB")
