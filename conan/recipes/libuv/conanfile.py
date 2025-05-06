import os
from conan import ConanFile
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout
from conan.tools.files import get, collect_libs, patch
from conan.errors import ConanInvalidConfiguration


class libuvConan(ConanFile):
    name = "libuv"
    version = "1.41.0"
    license = "MIT"
    url = "https://github.com/conan-io/conan-center-index"
    homepage = "https://libuv.org"
    description = "A multi-platform support library with a focus on asynchronous I/O"
    topics = ("libuv", "asynchronous", "io", "networking", "multi-platform", "conan-recipe")
    settings = "os", "compiler", "build_type", "arch"
    options = {
        "shared": [True, False],
        "fPIC": [True, False]
    }
    default_options = {
        "shared": False,
        "fPIC": True
    }
    exports_sources = [
        "CMakeLists.txt",
        "patches/*"
    ]

    @property
    def _source_subfolder(self):
        return "source_subfolder"

    def config_options(self):
        if self.settings.os == "Windows":
            del self.options.fPIC

    def configure(self):
        if self.options.shared:
            del self.options.fPIC
        if self.settings.compiler == "msvc":
            if int(self.settings.compiler.version.value) < 14:
                raise ConanInvalidConfiguration("Visual Studio 2015 or higher required")

    def source(self):
        get(self, **self.conan_data["sources"][self.version])
        os.rename("libuv-{}".format(self.version), self._source_subfolder)

    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()
        tc = CMakeToolchain(self)
        tc.cache_variables["LIBUV_BUILD_TESTS"] = False

        # TODO: remove this after updating to version newer than 1.41.0
        tc.cache_variables["CMAKE_POLICY_VERSION_MINIMUM"] = "3.24"
        tc.generate()

    def layout(self):
        cmake_layout(self)

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        for patch_entry in self.conan_data.get("patches", {}).get(self.version, []):
            patch(self, base_path=patch_entry['base_path'], patch_file=patch_entry['patch_file'])
        cmake.build()

    def package(self):
        cmake = CMake(self)
        cmake.install()

    def package_info(self):
        self.cpp_info.set_property("cmake_file_name", "libuv")
        self.cpp_info.libs = ["uv" if self.options.shared else "uv_a"]
        self.cpp_info.set_property("pkg_config_name", "libuv" if self.options.shared else "libuv-static")
        if self.options.shared:
            self.cpp_info.defines = ["USING_UV_SHARED=1"]
        if self.settings.os == "Linux":
            self.cpp_info.system_libs = ["dl", "pthread", "rt"]
        if self.settings.os == "Windows":
            self.cpp_info.system_libs = ["iphlpapi", "psapi", "userenv", "ws2_32"]
