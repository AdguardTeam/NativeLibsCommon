from conan import ConanFile
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout
from conan.tools.files import patch


class NGHttp3Conan(ConanFile):
    name = "nghttp3"
    version = "1.0.0"
    settings = "os", "compiler", "build_type", "arch"
    options = {}
    default_options = {}
    exports_sources = ["CMakeLists.txt", "patches/popcnt_old_cpu_fix.patch"]

    def source(self):
        self.run("git clone https://github.com/ngtcp2/nghttp3.git source_subfolder")
        self.run(f"cd source_subfolder && git checkout v{self.version}")
        patch(self, base_path="source_subfolder", patch_file="patches/popcnt_old_cpu_fix.patch")

    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()
        tc = CMakeToolchain(self)
        tc.variables["ENABLE_LIB_ONLY"] = "ON"
        tc.variables["ENABLE_STATIC_LIB"] = "ON"
        tc.variables["ENABLE_SHARED_LIB"] = "OFF"
        if tc.variables.get("BUILD_TYPE") == "Debug":
            tc.variables["DEBUGBUILD"] = "1"
        tc.generate()

    def layout(self):
        cmake_layout(self)

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        self.copy("*.h", dst="include/nghttp3", src="source_subfolder/lib/includes/nghttp3")
        self.copy("*.lib", dst="lib", keep_path=False)
        self.copy("*.a", dst="lib", keep_path=False)

    def package_info(self):
        self.cpp_info.libs = ["nghttp3"]
        self.cpp_info.defines.append("NGHTTP3_STATICLIB")
