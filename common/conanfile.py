from conans import ConanFile, CMake


class NativeLibsCommon(ConanFile):
    name = "native-libs-common"
    version = "777"  # use the `commit_hash` option to select the desired library version
    license = "Apache-2.0"
    author = "AdguardTeam"
    utl = "https://github.com/AdguardTeam/NativeLibsCommon"
    generators = "cmake"

    def requirements(self):
        self.requires("fmt/8.0.1")
        self.requires("libevent/2.1.11@AdguardTeam/NativeLibsCommon")
        self.requires("pcre2/10.37@AdguardTeam/NativeLibsCommon")

    def build_requirements(self):
        self.build_requires("gtest/1.11.0")

    def source(self):
        self.run("git clone https://github.com/AdguardTeam/NativeLibsCommon.git source_subfolder")

        if self.options.commit_hash:
            self.run("cd source_subfolder && git checkout %s" % self.options.commit_hash)
