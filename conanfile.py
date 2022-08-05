from conans import ConanFile, CMake
from conans.model.version import Version

class NativeLibsCommon(ConanFile):
    name = "native_libs_common"
    license = "Apache-2.0"
    author = "AdguardTeam"
    url = "https://github.com/AdguardTeam/NativeLibsCommon"
    description = "Common library for C++ opensource projects"
    generators = "cmake"
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

    def requirements(self):
        for req in self.conan_data["requirements"]:
            self.requires(req)

    def build_requirements(self):
        self.build_requires("gtest/1.11.0")

    def configure(self):
        self.options["gtest"].build_gmock = False
        if (self.version is None) or (Version(self.version) >= "1.0.20"):
            self.options["pcre2"].build_pcre2grep = False

    def config_options(self):
        if self.settings.os == "Windows":
            del self.options.fPIC

    def source(self):
        self.run("git init . && git remote add origin https://github.com/AdguardTeam/NativeLibsCommon.git && git fetch")

        if self.version == "777":
            if self.options.commit_hash:
                self.run("git checkout -f %s" % self.options.commit_hash)
        else:
            version_hash = self.conan_data["commit_hash"][self.version]["hash"]
            self.run("git checkout -f %s" % version_hash)

    def build(self):
        cmake = CMake(self)
        # A better way to pass these was not found :(
        if self.settings.os == "Linux":
            if self.settings.compiler.libcxx:
                cxx = "%s" % self.settings.compiler.libcxx
                cxx = cxx.rstrip('1')
                cmake.definitions["CMAKE_CXX_FLAGS"] = "-stdlib=%s" % cxx
            if self.settings.compiler.version:
                cmake.definitions["CMAKE_CXX_COMPILER_VERSION"] = self.settings.compiler.version
        if self.settings.os == "Macos":
            cmake.definitions["TARGET_OS"] = "macos"
        cmake.configure(source_folder=".", build_folder="build")
        cmake.build()

    def package(self):
        MODULES = ["common"]
        for m in MODULES:
            self.copy("*.h", dst="include", src="%s/include" % m, keep_path=True)
            self.copy("*.lib", dst="lib", src="build/%s" % m, keep_path=False)
            self.copy("*.a", dst="lib", src="build/%s" % m, keep_path=False)

    def package_info(self):
        self.cpp_info.name = "native_libs_common"
        self.cpp_info.includedirs = ["include"]
        self.cpp_info.libs = ["ag_common"]
        self.cpp_info.libdirs = ["lib"]
        self.cpp_info.requires = [
            "pcre2::pcre2",
            "libevent::libevent",
            "fmt::fmt",
        ]
        self.cpp_info.defines.append("FMT_EXCEPTIONS=0")
