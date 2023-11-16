from conan import ConanFile
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout
from conan.tools.files import patch

class BoringsslConan(ConanFile):
    name = "openssl"
    version = "boring-2021-05-11"
    settings = "os", "compiler", "build_type", "arch"
    options = {"shared": [True, False], "fPIC": [True, False]}
    default_options = {"shared": False, "fPIC": True}
    exports_sources = ["CMakeLists.txt", "patches/*"]

    def config_options(self):
        if self.settings.os == "Windows":
            del self.options.fPIC

    def source(self):
        self.run("git clone https://boringssl.googlesource.com/boringssl source_subfolder")
        self.run("cd source_subfolder && git checkout 8349dfc87e46d5914b0fefbb33241a95a9eef07d")
        patch(self, base_path="source_subfolder", patch_file="patches/00_warnings.patch")

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
        self.copy("*.h", dst="include", src="source_subfolder/include")
        self.copy("bin/bssl", dst="bin", keep_path=False)
        self.copy("*.dll", dst="bin", keep_path=False)
        self.copy("*.lib", dst="lib", keep_path=False)
        self.copy("*.so", dst="lib", keep_path=False)
        self.copy("*.dylib", dst="lib", keep_path=False)
        self.copy("*.a", dst="lib", keep_path=False)

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
