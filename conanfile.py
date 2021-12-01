from conans import ConanFile, CMake


class NativeLibsCommon(ConanFile):
    name = "native_libs_common"
    version = "777"  # use the `commit_hash` option to select the desired library version
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
        self.requires("fmt/8.0.1")
        self.requires("libevent/2.1.11@AdguardTeam/NativeLibsCommon")
        self.requires("pcre2/10.37@AdguardTeam/NativeLibsCommon")

    def build_requirements(self):
        self.build_requires("gtest/1.11.0")

    def configure(self):
        self.options["gtest"].build_gmock = False

    def config_options(self):
        if self.settings.os == "Windows":
            del self.options.fPIC

    def source(self):
        self.run("git clone ssh://git@bit.adguard.com:7999/adguard-core-libs/native-libs-common.git source_subfolder")
        self.run("cd source_subfolder && git checkout %s" % self.options.commit_hash)

#        if self.options.commit_hash:
#        else:
#            Err()

    def build(self):
        cmake = CMake(self)
        # A better way to pass these was not found :(
        #if self.settings.os == "Linux":
        #    if self.settings.compiler.libcxx:
        #        cmake.definitions["CMAKE_CXX_FLAGS"] = "-stdlib=%s" % self.settings.compiler.libcxx
        #    if self.settings.compiler.version:
        #        cmake.definitions["CMAKE_CXX_COMPILER_VERSION"] = self.settings.compiler.version
        #if self.settings.os == "Macos":
        #    cmake.definitions["TARGET_OS"] = "macos"
        cmake.configure(source_folder="source_subfolder")
        cmake.build(target="native_libs_common")

    def package(self):
        MODULES = ["common"]
        for m in MODULES:
            self.copy("*.h", dst="include", src="source_subfolder/%s/include" % m, keep_path=False)
            self.copy("*.lib", dst="lib", src="source_subfolder/%s" % m, keep_path=False)
            self.copy("*.a", dst="lib", src="source_subfolder/%s" % m, keep_path=False)

    def package_info(self):
        self.cpp_info.name = "native_libs_common"
        self.cpp_info.includedirs = ["include"]
        self.cpp_info.libs = ["common"]
        self.cpp_info.libdirs = ["lib"]
        self.cpp_info.requires = [
            "pcre2::pcre2",
            "libevent::libevent",
            "fmt::fmt",
        ]
