from conans import ConanFile, CMake, tools
import os


# Needed because `protobuf` from the center cannot be built on Windows with clang-cl
# https://github.com/protocolbuffers/protobuf/issues/6503
class ProtobufConan(ConanFile):
    name = "protobuf"
    version = "3.18.0"
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
        url = "https://github.com/protocolbuffers/protobuf/releases/download/v%s/protobuf-cpp-%s.zip" \
              % (self.version, self.version)
        # downloading here manually (not via git) is a workaround for strange "No such file" errors
        # on Windows
        tools.download(url, zip_name, sha256="627e80a0c8ee6733a218813b75babd5414af5a46cb08d0421cd346fd6c45b76d")
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
