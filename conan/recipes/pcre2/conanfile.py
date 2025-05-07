from conan import ConanFile
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout
from conan.tools.files import get, rmdir, replace_in_file, copy
from conan.errors import ConanInvalidConfiguration
import os

class PCRE2Conan(ConanFile):
    name = "pcre2"
    version = "10.37"
    url = "https://github.com/conan-io/conan-center-index"
    homepage = "https://www.pcre.org/"
    description = "Perl Compatible Regular Expressions"
    topics = ("regex", "regexp", "PCRE")
    license = "BSD-3-Clause"

    settings = "os", "arch", "compiler", "build_type"
    options = {
        "shared": [True, False],
        "fPIC": [True, False],
        "build_pcre2_8": [True, False],
        "build_pcre2_16": [True, False],
        "build_pcre2_32": [True, False],
        "build_pcre2grep": [True, False],
        "with_zlib": [True, False],
        "with_bzip2": [True, False],
        "support_jit": [True, False]
    }
    default_options = {
        "shared": False,
        "fPIC": True,
        "build_pcre2_8": True,
        "build_pcre2_16": True,
        "build_pcre2_32": True,
        "build_pcre2grep": True,
        "with_zlib": True,
        "with_bzip2": True,
        "support_jit": False
    }

    exports_sources = "CMakeLists.txt"
    _cmake = None

    @property
    def _source_subfolder(self):
        return self.source_folder + "/source_subfolder"

    @property
    def _build_subfolder(self):
        return self.build_folder + "/build_subfolder"

    def config_options(self):
        if self.settings.os == "Windows":
            del self.options.fPIC

    def configure(self):
        if self.options.shared:
            del self.options.fPIC4
        if not self.options.build_pcre2grep:
            del self.options.with_zlib
            del self.options.with_bzip2
        if not self.options.build_pcre2_8 and not self.options.build_pcre2_16 and not self.options.build_pcre2_32:
            raise ConanInvalidConfiguration("At least one of build_pcre2_8, build_pcre2_16 or build_pcre2_32 must be enabled")
        if self.options.build_pcre2grep and not self.options.build_pcre2_8:
            raise ConanInvalidConfiguration("build_pcre2_8 must be enabled for the pcre2grep program")

    def requirements(self):
        if self.options.get_safe("with_zlib"):
            self.requires("zlib/1.2.11")
        if self.options.get_safe("with_bzip2"):
            self.requires("bzip2/1.0.8")

    def source(self):
        get(self, **self.conan_data["sources"][self.version])
        extracted_dir = self.name + "-" + self.version
        os.rename(extracted_dir, self._source_subfolder)

    def layout(self):
        cmake_layout(self)

    def _patch_sources(self):
        # Do not add ${PROJECT_SOURCE_DIR}/cmake because it contains a custom
        # FindPackageHandleStandardArgs.cmake which can break conan generators
        cmakelists = os.path.join(self._source_subfolder, "CMakeLists.txt")
        if self.version < "10.34":
            replace_in_file(self, cmakelists, "SET(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)", "")
        else:
            replace_in_file(self, cmakelists, "LIST(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)", "")


    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()
        tc = CMakeToolchain(self)
        tc.cache_variables["PCRE2_BUILD_PCRE2GREP"] = self.options.build_pcre2grep
        tc.cache_variables["PCRE2_SUPPORT_LIBZ"] = self.options.get_safe("with_zlib", False)
        tc.cache_variables["PCRE2_SUPPORT_LIBBZ2"] = self.options.get_safe("with_bzip2", False)
        tc.cache_variables["PCRE2_BUILD_TESTS"] = False
        if self.settings.os == "Windows" and self.settings.compiler == "msvc":
            runtime = not self.options.shared and "MT" in self.settings.compiler.runtime
            tc.cache_variables["PCRE2_STATIC_RUNTIME"] = runtime
        tc.cache_variables["PCRE2_DEBUG"] = self.settings.build_type == "Debug"
        tc.cache_variables["PCRE2_BUILD_PCRE2_8"] = self.options.build_pcre2_8
        tc.cache_variables["PCRE2_BUILD_PCRE2_16"] = self.options.build_pcre2_16
        tc.cache_variables["PCRE2_BUILD_PCRE2_32"] = self.options.build_pcre2_32
        tc.cache_variables["PCRE2_SUPPORT_JIT"] = self.options.support_jit

        # TODO: remove this after updating to version newer than 10.37
        tc.cache_variables["CMAKE_POLICY_VERSION_MINIMUM"] = "3.24"
        tc.generate()

    def layout(self):
        cmake_layout(self)

    def build(self):
        cmake = CMake(self)
        self._patch_sources()
        cmake.configure()
        cmake.build()

    def package(self):
        copy(self, pattern="LICENCE", dst="licenses", src=self._source_subfolder)
        cmake = CMake(self)
        cmake.install()
        rmdir(self, os.path.join(self.package_folder, "man"))
        rmdir(self, os.path.join(self.package_folder, "share"))
        rmdir(self, os.path.join(self.package_folder, "lib", "pkgconfig"))

    def package_info(self):
        self.cpp_info.names["pkg_config"] = "libpcre2"
        if self.options.build_pcre2_8:
            # pcre2-8
            self.cpp_info.components["pcre2-8"].names["pkg_config"] = "libpcre2-8"
            self.cpp_info.components["pcre2-8"].libs = [self._lib_name("pcre2-8")]
            if not self.options.shared:
                self.cpp_info.components["pcre2-8"].defines.append("PCRE2_STATIC")
            # pcre2-posix
            self.cpp_info.components["pcre2-posix"].names["pkg_config"] = "libpcre2-posix"
            self.cpp_info.components["pcre2-posix"].libs = [self._lib_name("pcre2-posix")]
            self.cpp_info.components["pcre2-posix"].requires = ["pcre2-8"]
        # pcre2-16
        if self.options.build_pcre2_16:
            self.cpp_info.components["pcre2-16"].names["pkg_config"] = "libpcre2-16"
            self.cpp_info.components["pcre2-16"].libs = [self._lib_name("pcre2-16")]
            if not self.options.shared:
                self.cpp_info.components["pcre2-16"].defines.append("PCRE2_STATIC")
        # pcre2-32
        if self.options.build_pcre2_32:
            self.cpp_info.components["pcre2-32"].names["pkg_config"] = "libpcre2-32"
            self.cpp_info.components["pcre2-32"].libs = [self._lib_name("pcre2-32")]
            if not self.options.shared:
                self.cpp_info.components["pcre2-32"].defines.append("PCRE2_STATIC")

        if self.options.build_pcre2grep:
            bin_path = os.path.join(self.package_folder, "bin")
            self.output.info("Appending PATH environment variable: {}".format(bin_path))
            self.env_info.PATH.append(bin_path)
            # FIXME: This is a workaround to avoid ConanException. zlib and bzip2
            # are optional requirements of pcre2grep executable, not of any pcre2 lib.
            if self.options.with_zlib:
                self.cpp_info.components["pcre2-8"].requires.append("zlib::zlib")
            if self.options.with_bzip2:
                self.cpp_info.components["pcre2-8"].requires.append("bzip2::bzip2")

    def _lib_name(self, name):
        libname = name
        if self.settings.os == "Windows":
            if self.settings.build_type == "Debug":
                libname += "d"
            if self.settings.compiler == "gcc" and self.options.shared:
                libname += ".dll"
        return libname
