from conan import ConanFile
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout
from conan.tools.files import copy
from os.path import join


# Needed because `libnghttp2` from the center cannot be built on MacOS with our compilation flags
class NGHttp2Conan(ConanFile):
    name = "nghttp2"
    version = "1.44.0"
    settings = "os", "compiler", "build_type", "arch"
    options = {"shared": [True, False], "fPIC": [True, False]}
    default_options = {"shared": False, "fPIC": True}
    exports_sources = ["CMakeLists.txt"]

    def config_options(self):
        if self.settings.os == "Windows":
            del self.options.fPIC

    def source(self):
        self.run("git clone https://github.com/nghttp2/nghttp2.git source_subfolder")
        self.run(f"cd source_subfolder && git checkout v{self.version}")

    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()
        tc = CMakeToolchain(self)
        tc.cache_variables["ENABLE_LIB_ONLY"] = "ON"
        if self.options.shared:
            tc.cache_variables["ENABLE_STATIC_LIB"] = "OFF"
            tc.cache_variables["ENABLE_SHARED_LIB"] = "ON"
        else:
            tc.cache_variables["ENABLE_STATIC_LIB"] = "ON"
            tc.cache_variables["ENABLE_SHARED_LIB"] = "OFF"
        if tc.cache_variables.get("BUILD_TYPE") == "Debug":
            tc.cache_variables["DEBUGBUILD"] = "1"

        # TODO: remove this after updating to version newer than 1.44.0
        tc.cache_variables["CMAKE_POLICY_VERSION_MINIMUM"] = "3.24"
        tc.generate()

    def layout(self):
        cmake_layout(self)

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        copy(self, "*.h", src=join(self.source_folder, "source_subfolder/lib/includes/nghttp2"), dst=join(self.package_folder, "include/nghttp2"), keep_path = True)
        copy(self, "*.lib", self.build_folder, dst=join(self.package_folder, "lib"), keep_path=False)
        copy(self, "*.a", self.build_folder, dst=join(self.package_folder, "lib"), keep_path=False)

    def package_info(self):
        self.cpp_info.libs = ["nghttp2"]
        self.cpp_info.defines.append("NGHTTP2_STATICLIB")
