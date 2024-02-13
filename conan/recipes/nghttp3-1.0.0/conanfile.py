from conan import ConanFile
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout
from conan.tools.files import patch, copy
from os.path import join


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
        tc.cache_variables["ENABLE_LIB_ONLY"] = "ON"
        tc.cache_variables["ENABLE_STATIC_LIB"] = "ON"
        tc.cache_variables["ENABLE_SHARED_LIB"] = "OFF"
        if tc.cache_variables.get("BUILD_TYPE") == "Debug":
            tc.cache_variables["DEBUGBUILD"] = "1"
        tc.generate()

    def layout(self):
        cmake_layout(self)

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        copy(self, "*.h", src=join(self.build_folder, "source_subfolder/lib/includes/nghttp3"), dst=join(self.package_folder, "include/nghttp3"), keep_path = True)
        copy(self, "*.h", src=join(self.source_folder, "source_subfolder/lib/includes/nghttp3"), dst=join(self.package_folder, "include/nghttp3"), keep_path = True)
        copy(self, "*.lib", self.build_folder, dst=join(self.package_folder, "lib"), keep_path=False)
        copy(self, "*.a", self.build_folder, dst=join(self.package_folder, "lib"), keep_path=False)

    def package_info(self):
        self.cpp_info.libs = ["nghttp3"]
        self.cpp_info.defines.append("NGHTTP3_STATICLIB")
