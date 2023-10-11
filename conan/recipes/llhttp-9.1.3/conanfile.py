from conan import ConanFile
from conan.tools.cmake import CMake, CMakeToolchain, cmake_layout
from conan.tools.files import copy, get, rmdir
import os

required_conan_version = ">=1.53.0"

# Based on https://github.com/conan-io/conan-center-index/blob/master/recipes/llhttp/all/conanfile.py.
# Using custom recipe because the default one ships DLL even in case `shared = False`
# possibly causing accidental linking against the dynamic library.
class LlhttpParserConan(ConanFile):
    name = "llhttp"
    version = "9.1.3"
    description = "http request/response parser for c"
    topics = ("http", "parser")
    url = "https://github.com/conan-io/conan-center-index"
    homepage = "https://github.com/nodejs/llhttp"
    license = "MIT"
    package_type = "library"
    settings = "os", "arch", "compiler", "build_type"
    options = {
        "shared": [True, False],
        "fPIC": [True, False],
    }
    default_options = {
        "shared": False,
        "fPIC": True,
    }

    def config_options(self):
        if self.settings.os == "Windows":
            del self.options.fPIC

    def configure(self):
        if self.options.shared:
            self.options.rm_safe("fPIC")
        self.settings.rm_safe("compiler.cppstd")
        self.settings.rm_safe("compiler.libcxx")

    def layout(self):
        cmake_layout(self, src_folder="src")

    def source(self):
        get(self,
            url=f"https://github.com/nodejs/llhttp/archive/refs/tags/release/v{self.version}.tar.gz",
            sha256="49405a7bcb4312b29b91408ee1395de3bc3b29e3bdd10380dc4eb8210912f295",
            strip_root=True)

    def generate(self):
        tc = CMakeToolchain(self)
        tc.variables["CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS"] = True
        if not self.options.shared:
            tc.variables["BUILD_SHARED_LIBS"] = False
            tc.variables["BUILD_STATIC_LIBS"] = True
        else:
            tc.variables["BUILD_SHARED_LIBS"] = True
            tc.variables["BUILD_STATIC_LIBS"] = False
        tc.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        copy(self, "LICENSE-MIT", src=self.source_folder, dst=os.path.join(self.package_folder, "licenses"))
        cmake = CMake(self)
        cmake.install()
        rmdir(self, os.path.join(self.package_folder, "lib", "cmake"))

    def package_info(self):
        self.cpp_info.set_property("cmake_file_name", "llhttp")
        self.cpp_info.set_property("cmake_target_name", "llhttp::llhttp")
        self.cpp_info.set_property("pkg_config_name", "libllhttp")
        self.cpp_info.libs = ["llhttp"]
