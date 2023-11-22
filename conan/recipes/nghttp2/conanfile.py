from conans import ConanFile, CMake


# Needed because `libnghttp2` from the center cannot be built on MacOS with our compilation flags
class NGHttp2Conan(ConanFile):
    name = "nghttp2"
    version = "1.44.0"
    settings = "os", "compiler", "build_type", "arch"
    options = {"shared": [True, False], "fPIC": [True, False]}
    default_options = {"shared": False, "fPIC": True}
    generators = "cmake"
    exports_sources = ["CMakeLists.txt"]

    def config_options(self):
        if self.settings.os == "Windows":
            del self.options.fPIC

    def source(self):
        self.run("git clone https://github.com/nghttp2/nghttp2.git source_subfolder")
        self.run(f"cd source_subfolder && git checkout v{self.version}")

    def build(self):
        cmake = CMake(self)
        cmake.definitions["ENABLE_LIB_ONLY"] = "ON"
        if self.options.shared:
            cmake.definitions["ENABLE_STATIC_LIB"] = "OFF"
            cmake.definitions["ENABLE_SHARED_LIB"] = "ON"
        else:
            cmake.definitions["ENABLE_STATIC_LIB"] = "ON"
            cmake.definitions["ENABLE_SHARED_LIB"] = "OFF"
        if cmake.build_type == "Debug":
            cmake.definitions["DEBUGBUILD"] = "1"
        cmake.configure()
        cmake.build()

    def package(self):
        self.copy("*.h", dst="include/nghttp2", src="source_subfolder/lib/includes/nghttp2")
        self.copy("*.lib", dst="lib", keep_path=False)
        self.copy("*.a", dst="lib", keep_path=False)

    def package_info(self):
        self.cpp_info.libs = ["nghttp2"]
        self.cpp_info.defines.append("NGHTTP2_STATICLIB")
