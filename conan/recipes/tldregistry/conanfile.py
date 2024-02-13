from conan import ConanFile
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout
from conan.tools.files import copy
from os.path import join

class LdnsConan(ConanFile):
    name = "tldregistry"
    version = "2022-12-26"
    settings = "os", "compiler", "build_type", "arch"
    options = {}
    default_options = {}
    requires = []
    exports_sources = ["include/*", "src/*", "chromium/*", "CMakeLists.txt"]

    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()
        tc = CMakeToolchain(self)
        tc.generate()

    def layout(self):
        cmake_layout(self)

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        copy(self, "*.h", src=join(self.source_folder, "include/tldregistry"), dst=join(self.package_folder, "include/tldregistry"), keep_path = True)
        copy(self, "*.lib", src=self.build_folder, dst=join(self.package_folder, "lib"), keep_path=False)
        copy(self, "*.a", src=self.build_folder, dst=join(self.package_folder, "lib"), keep_path=False)

    def package_info(self):
        self.cpp_info.libs = ["tldregistry"]
