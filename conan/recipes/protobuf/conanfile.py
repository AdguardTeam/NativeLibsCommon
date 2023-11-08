from conans import ConanFile, CMake, tools
import os


# Needed because `protobuf` from the center cannot be built on Windows with clang-cl
# https://github.com/protocolbuffers/protobuf/issues/6503
class ProtobufConan(ConanFile):
    name = "protobuf"
    version = "3.21.12"
    source_subfolder = "protobuf-%s/src" % version
    settings = "os", "compiler", "build_type", "arch"
    options = {"shared": [True, False], "fPIC": [True, False]}
    default_options = {"shared": False, "fPIC": True}
    generators = "cmake"
    exports_sources = ["CMakeLists.txt"]

    def config_options(self):
        if self.settings.os == "Windows":
            del self.options.fPIC

    def source(self):
        zip_name = "protobuf.zip"
        url = "https://github.com/protocolbuffers/protobuf/releases/download/v21.12/protobuf-cpp-%s.zip" \
              % (self.version)
        # downloading here manually (not via git) is a workaround for strange "No such file" errors
        # on Windows
        tools.download(url, zip_name, sha256="c7db1d1fead682be24aa29477f5e224fdfc2bb1adeeaee1214f854a2076de71e")
        tools.unzip(zip_name)
        os.unlink(zip_name)

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build(target="protobuf")

    def package(self):
        self.copy("*.h", dst="include", src=self.source_subfolder)
        self.copy("*.inc", dst="include", src=self.source_subfolder)
        self.copy("*.lib", dst="lib", keep_path=False)
        self.copy("*.a", dst="lib", keep_path=False)

    def package_info(self):
        self.cpp_info.libs = ["protobuf"]
