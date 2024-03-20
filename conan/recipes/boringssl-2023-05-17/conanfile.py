from conan import ConanFile
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout
from conan.tools.files import copy, get, patch
from os.path import join
import os

class BoringsslConan(ConanFile):
    name = "openssl"
    version = "boring-2023-05-17" # Last version with Windows 7 support
    settings = "os", "compiler", "build_type", "arch"
    options = {"shared": [True, False], "fPIC": [True, False]}
    default_options = {"shared": False, "fPIC": True}
    exports_sources = ["CMakeLists.txt", "patches/*"]

    def config_options(self):
        if self.settings.os == "Windows":
            del self.options.fPIC

    def source(self):
        get(self,
            url="https://boringssl.googlesource.com/boringssl/+archive/dd5219451c3ce26221762a15d867edf43b463bb2.tar.gz",
            destination="source_subfolder")

        # Apply all patches from the `patches` directory
        patches_path = os.path.join("patches")
        patches = sorted([f for f in os.listdir(patches_path) if os.path.isfile(os.path.join(patches_path, f))])
        for patch_name in patches:
            patch(self, base_path="source_subfolder", patch_file=os.path.join(patches_path, patch_name))

    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()
        tc = CMakeToolchain(self)
        if (self.settings.os == "Windows" and (self.settings.arch == "armv8" or self.settings.arch == "armv7")) or self.settings.arch == "mips":
            tc.cache_variables["OPENSSL_NO_ASM"] = "ON"
        tc.generate()

    def layout(self):
        cmake_layout(self)

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        copy(self, "*.h", src=join(self.source_folder, "source_subfolder/include"), dst=join(self.package_folder, "include"), keep_path = True)
        copy(self, "bin/bssl", src=self.build_folder, dst=join(self.package_folder, "bin"), keep_path=False)
        copy(self, "*.dll", src=self.build_folder, dst=join(self.package_folder, "bin"), keep_path=False)
        copy(self, "*.lib", src=self.build_folder, dst=join(self.package_folder, "lib"), keep_path=False)
        copy(self, "*.so", src=self.build_folder, dst=join(self.package_folder, "lib"), keep_path=False)
        copy(self, "*.dylib", src=self.build_folder, dst=join(self.package_folder, "lib"), keep_path=False)
        copy(self, "*.a", src=self.build_folder, dst=join(self.package_folder, "lib"), keep_path=False)


    def package_info(self):
        self.cpp_info.set_property("cmake_file_name", "OpenSSL")
        self.cpp_info.set_property("cmake_find_mode", "both")
        self.cpp_info.names["cmake_find_package"] = "OpenSSL"
        self.cpp_info.components["crypto"].set_property("cmake_target_name", "OpenSSL::Crypto")
        self.cpp_info.components["crypto"].set_property("pkg_config_name", "libcrypto")
        self.cpp_info.components["ssl"].set_property("cmake_target_name", "OpenSSL::SSL")
        self.cpp_info.components["ssl"].set_property("pkg_config_name", "libssl")
        self.cpp_info.components["crypto"].names["cmake_find_package"] = "Crypto"
        self.cpp_info.components["crypto"].names["cmake_find_package_multi"] = "Crypto"
        self.cpp_info.components["ssl"].names["cmake_find_package"] = "SSL"
        self.cpp_info.components["ssl"].names["cmake_find_package_multi"] = "SSL"
        self.cpp_info.components["ssl"].libs = ["ssl"]
        self.cpp_info.components["crypto"].libs = ["crypto"]
        if self.settings.os == "Windows":
            self.cpp_info.system_libs = ["bcrypt"]
