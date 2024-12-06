from conan import ConanFile
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout
from conan.tools.files import patch, copy
from os.path import join
import re

required_conan_version = ">=1.53.0"

class NativeLibsCommon(ConanFile):
    name = "native_libs_common"
    license = "Apache-2.0"
    author = "AdguardTeam"
    url = "https://github.com/AdguardTeam/NativeLibsCommon"
    vcs_url = "https://github.com/AdguardTeam/NativeLibsCommon.git"
    description = "Common library for C++ opensource projects"
    settings = "os", "compiler", "build_type", "arch"
    options = {
        "shared": [True, False],
        "fPIC": [True, False],
    }
    default_options = {
        "shared": False,
        "fPIC": True,
    }
    # A list of paths to patches. The paths must be relative to the conanfile directory.
    # They are applied in case of the version equals 777 and mostly intended to be used
    # for testing.
    patch_files = []
    exports_sources = patch_files

    def requirements(self):
        self.requires("fmt/10.1.1", transitive_headers=True)
        self.requires("libevent/2.1.11@adguard_team/native_libs_common", transitive_headers=True)
        self.requires("llhttp/9.1.3@adguard_team/native_libs_common", transitive_headers=True)
        self.requires("magic_enum/0.9.5", transitive_headers=True)
        self.requires("nghttp2/1.56.0@adguard_team/native_libs_common", transitive_headers=True)
        self.requires("nghttp3/1.0.0@adguard_team/native_libs_common", transitive_headers=True)
        self.requires("ngtcp2/1.0.1@adguard_team/native_libs_common", transitive_headers=True)
        if "mips" in str(self.settings.arch):
            self.requires("openssl/3.1.5-quic1@adguard_team/native_libs_common", transitive_headers=True, force=True)
        else:
            self.requires("openssl/boring-2024-09-13@adguard_team/native_libs_common", transitive_headers=True)
        self.requires("pcre2/10.37@adguard_team/native_libs_common", transitive_headers=True)

    def build_requirements(self):
        self.test_requires("gtest/1.14.0")

    def configure(self):
        self.options["gtest"].build_gmock = False
        self.options["llhttp"].shared = False
        if (self.version is None) or (self.version >= "1.0.20"):
            self.options["pcre2"].build_pcre2grep = False

    def config_options(self):
        if self.settings.os == "Windows":
            del self.options.fPIC

    def source(self):
        self.run(f"git init . && git remote add origin {self.vcs_url} && git fetch")
        if re.match(r'\d+\.\d+\.\d+', self.version) is not None:
            version_hash = self.conan_data["commit_hash"][self.version]["hash"]
            self.run("git checkout -f %s" % version_hash)
        else:
            self.run("git checkout -f %s" % self.version)
        for p in self.patch_files:
            patch(self, patch_file=p)

    def generate(self):
        tc = CMakeToolchain(self)
        tc.generate()
        deps = CMakeDeps(self)
        deps.generate()

    def layout(self):
        cmake_layout(self)

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        MODULES = ["common", "http"]
        for m in MODULES:
            copy(self, "*.h", src=join(self.source_folder, "%s/include" % m), dst=join(self.package_folder, "include"), keep_path=True)
        copy(self, "*.lib", src=self.build_folder, dst=join(self.package_folder, "lib"), keep_path=False)
        copy(self, "*.a", src=self.build_folder, dst=join(self.package_folder, "lib"), keep_path=False)

    def package_info(self):
        self.cpp_info.names["cmake_find_package"] = "native_libs_common"
        self.cpp_info.name = "native_libs_common"
        self.cpp_info.includedirs = ["include"]
        self.cpp_info.libs = ["ag_common", "ag_common_http"]
        self.cpp_info.libdirs = ["lib"]
        self.cpp_info.requires = [
            "fmt::fmt",
            "libevent::libevent",
            "llhttp::llhttp",
            "magic_enum::magic_enum",
            "nghttp2::nghttp2",
            "nghttp3::nghttp3",
            "ngtcp2::ngtcp2",
            "openssl::openssl",
            "pcre2::pcre2",
        ]
        if self.settings.os == "Windows":
            self.cpp_info.system_libs = ["ws2_32", "iphlpapi", "ntdll"]
        self.cpp_info.defines.append("FMT_EXCEPTIONS=0")
