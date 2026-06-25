from conan import ConanFile
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout
from conan.tools.files import copy, get, patch, rmdir
import os

required_conan_version = ">=1.53.0"

# Based on https://github.com/conan-io/conan-center-index/blob/master/recipes/llhttp/all/conanfile.py.
# Using custom recipe because the default one ships DLL even in case `shared = False`
# possibly causing accidental linking against the dynamic library.
class LlhttpParserConan(ConanFile):
    name = "llhttp"
    version = "9.3.0"
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

    exports_sources = "patches/*.patch"

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
            sha256="1a2b45cb8dda7082b307d336607023aa65549d6f060da1d246b1313da22b685a",
            strip_root=True)
        # AdGuard patches to match the lenient behaviour of CoreLibs' http-parser.
        patch(self, patch_file=os.path.join(self.export_sources_folder, "patches", "0001-lenient-defaults.patch"), strip=1)
        patch(self, patch_file=os.path.join(self.export_sources_folder, "patches", "0002-status-205-no-body.patch"), strip=1)
        patch(self, patch_file=os.path.join(self.export_sources_folder, "patches", "0003-url-scheme.patch"), strip=1)

    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()
        tc = CMakeToolchain(self)
        tc.cache_variables["CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS"] = True
        if not self.options.shared:
            tc.cache_variables["BUILD_SHARED_LIBS"] = False
            tc.cache_variables["BUILD_STATIC_LIBS"] = True
        else:
            tc.cache_variables["BUILD_SHARED_LIBS"] = True
            tc.cache_variables["BUILD_STATIC_LIBS"] = False
        tc.generate()

    def layout(self):
        cmake_layout(self)

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
