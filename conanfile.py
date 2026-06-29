from conan import ConanFile
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout
from conan.tools.files import copy
from conan.tools.scm import Git
from os.path import join
import re, os, shutil

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
    def requirements(self):
        self.requires("fmt/12.1.0", transitive_headers=True)
        self.requires("libevent/2.1.11@adguard/oss", transitive_headers=True)
        self.requires("llhttp/9.3.0@adguard/oss", transitive_headers=True)
        self.requires("magic_enum/0.9.5", transitive_headers=True)
        self.requires("nghttp2/1.56.0@adguard/oss", transitive_headers=True)
        self.requires("nghttp3/1.0.0@adguard/oss", transitive_headers=True)
        self.requires("ngtcp2/1.22.1@adguard/oss", transitive_headers=True)
        if "mips" in str(self.settings.arch):
            self.requires("openssl/3.1.5-quic1@adguard/oss", transitive_headers=True, force=True)
        else:
            self.requires("openssl/boring-2024-09-13@adguard/oss", transitive_headers=True)
        self.requires("pcre2/10.37@adguard/oss", transitive_headers=True)

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

    def export_sources(self):
        # `conan create . --version local` builds from the working tree instead of
        # fetching a tag (there is no "vlocal" tag). Copy the git-tracked files into
        # the exported sources so source() can pick them up and skip the clone.
        if self.version == "local":
            git = Git(self)
            for i in git.included_files():
                dst = os.path.join(self.export_sources_folder, i)
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                shutil.copy2(i, dst)

    def source(self):
        # Local export: the working tree was already staged by export_sources().
        if os.listdir(self.source_folder):
            return
        version = str(self.version)
        # A "git describe" version looks like "<tag>-<n>-g<rev>"; check out the
        # commit after "-g". Any other version is a release tag "v<version>".
        described = re.search(r"-g([0-9a-f]+)$", version)
        ref = described.group(1) if described else "v%s" % version
        git = Git(self)
        git.clone(url=self.vcs_url, target=".")
        git.checkout(ref)

    def generate(self):
        tc = CMakeToolchain(self)
        tc.user_presets_path = False
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
        MODULES = ["common", "http", "tls"]
        for m in MODULES:
            copy(self, "*.h", src=join(self.source_folder, "%s/include" % m), dst=join(self.package_folder, "include"), keep_path=True)
        copy(self, "*.lib", src=self.build_folder, dst=join(self.package_folder, "lib"), keep_path=False)
        copy(self, "*.a", src=self.build_folder, dst=join(self.package_folder, "lib"), keep_path=False)

    def package_info(self):
        self.cpp_info.names["cmake_find_package"] = "native_libs_common"
        self.cpp_info.name = "native_libs_common"
        self.cpp_info.includedirs = ["include"]
        self.cpp_info.libs = ["ag_common", "ag_common_http", "ag_common_tls"]
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
            self.cpp_info.system_libs = ["ws2_32", "iphlpapi", "ntdll", "fwpuclnt"]
        else:
            if self.settings.os != "Android":
                self.cpp_info.system_libs = ["resolv"]
            if self.settings.os in ["Macos", "iOS"]:
                self.cpp_info.frameworks = ["Network"]
        self.cpp_info.defines.append("FMT_EXCEPTIONS=0")
