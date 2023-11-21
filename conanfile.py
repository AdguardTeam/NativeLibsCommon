from conan import ConanFile, Version
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout
from conan.tools.files import patch


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
        "commit_hash": "ANY",
    }
    default_options = {
        "shared": False,
        "fPIC": True,
        "commit_hash": None,  # None means `master`
    }
    # A list of paths to patches. The paths must be relative to the conanfile directory.
    # They are applied in case of the version equals 777 and mostly intended to be used
    # for testing.
    patch_files = []
    exports_sources = patch_files

    def requirements(self):
        self.requires("fmt/8.0.1")
        self.requires("libevent/2.1.11@AdguardTeam/NativeLibsCommon")
        self.requires("llhttp/9.1.3@AdguardTeam/NativeLibsCommon")
        self.requires("magic_enum/0.7.3")
        self.requires("nghttp2/1.56.0@AdguardTeam/NativeLibsCommon")
        self.requires("nghttp3/1.0.0@AdguardTeam/NativeLibsCommon")
        self.requires("ngtcp2/1.0.1@AdguardTeam/NativeLibsCommon")
        self.requires("openssl/boring-2023-05-17@AdguardTeam/NativeLibsCommon")
        self.requires("pcre2/10.37@AdguardTeam/NativeLibsCommon")

    def build_requirements(self):
        self.tool_requires("gtest/1.11.0")

    def configure(self):
        self.options["gtest"].build_gmock = False
        self.options["llhttp"].shared = False
        if (self.version is None) or (Version(self.version) >= "1.0.20"):
            self.options["pcre2"].build_pcre2grep = False

    def config_options(self):
        if self.settings.os == "Windows":
            del self.options.fPIC

    def source(self):
        self.run(f"git init . && git remote add origin {self.vcs_url} && git fetch")

        if self.version == "777":
            if self.options.commit_hash:
                self.run("git checkout -f %s" % self.options.commit_hash)
            else:
                self.run("git checkout -f master")

            for p in self.patch_files:
                patch(self, patch_file=p)
        else:
            version_hash = self.conan_data["commit_hash"][self.version]["hash"]
            self.run("git checkout -f %s" % version_hash)

    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()
        tc = CMakeToolchain(self)
        if self.settings.os == "Linux":
            if self.settings.compiler.libcxx:
                cxx = "%s" % self.settings.compiler.libcxx
                cxx = cxx.rstrip('1')
                tc.variables["CMAKE_CXX_FLAGS"] = "-stdlib=%s" % cxx
            if self.settings.compiler.version:
                tc.variables["CMAKE_CXX_COMPILER_VERSION"] = self.settings.compiler.version
        if self.settings.os == "Macos":
            tc.variables["TARGET_OS"] = "macos"

        tc.generate()

    def layout(self):
        cmake_layout(self)

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        MODULES = ["common", "http"]
        for m in MODULES:
            self.copy("*.h", dst="include", src="%s/include" % m, keep_path=True)
            self.copy("*.lib", dst="lib", src="build/%s" % m, keep_path=False)
            self.copy("*.a", dst="lib", src="build/%s" % m, keep_path=False)

    def package_info(self):
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
        self.cpp_info.defines.append("FMT_EXCEPTIONS=0")
        if self.settings.os == "Windows":
            self.cpp_info.system_libs = ["ws2_32", "iphlpapi", "ntdll"]
