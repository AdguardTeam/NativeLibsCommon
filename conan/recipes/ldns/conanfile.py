from conan import ConanFile
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout
from conan.tools.files import patch, copy
from os.path import join

class LdnsConan(ConanFile):
    name = "ldns"
    version = "2021-03-29"
    settings = "os", "compiler", "build_type", "arch"
    options = {"shared": [True, False], "fPIC": [True, False]}
    default_options = {"shared": False, "fPIC": True}
    requires = ["libevent/2.1.11@adguard_team/native_libs_common"]
    exports_sources = ["compat/*", "windows/*", "*.patch", "CMakeLists.txt"]

    def config_options(self):
        if self.settings.os == "Windows":
            del self.options.fPIC

    def source(self):
        self.run("git clone https://github.com/NLnetLabs/ldns.git")
        self.run("cd ldns && git checkout 7128ef56649e0737f236bc5d5d640de38ff0036d")
        patch(self, base_path="ldns", patch_file="windows.patch")

    def layout(self):
        cmake_layout(self)

    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()
        tc = CMakeToolchain(self)
        tc.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        copy(self, "*.h", src=join(self.source_folder, "ldns/ldns"), dst=join(self.package_folder, "include/ldns"), keep_path = True)
        copy(self, "*.h", src=join(self.source_folder, "compat/ldns"), dst=join(self.package_folder, "include/ldns"), keep_path = True)
        copy(self, "*.dll", src=self.build_folder, dst=join(self.package_folder, "bin"), keep_path=False)
        copy(self, "*.lib", src=self.build_folder, dst=join(self.package_folder, "lib"), keep_path=False)
        copy(self, "*.so", src=self.build_folder, dst=join(self.package_folder, "lib"), keep_path=False)
        copy(self, "*.dylib", src=self.build_folder, dst=join(self.package_folder, "lib"), keep_path=False)
        copy(self, "*.a", src=self.build_folder, dst=join(self.package_folder, "lib"), keep_path=False)

    def package_info(self):
        self.cpp_info.libs = ["ldns"]
