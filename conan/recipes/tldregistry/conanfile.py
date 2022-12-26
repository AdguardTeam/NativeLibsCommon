from conans import ConanFile, CMake


class LdnsConan(ConanFile):
    name = "tldregistry"
    version = "2022-12-26"
    settings = "os", "compiler", "build_type", "arch"
    options = {}
    default_options = {}
    generators = "cmake"
    requires = []
    exports_sources = ["include/*", "src/*", "chromium/*", "CMakeLists.txt"]

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        self.copy("*.h", dst="include/tldregistry", src="include/tldregistry")
        self.copy("*.lib", dst="lib", keep_path=False)
        self.copy("*.a", dst="lib", keep_path=False)

    def package_info(self):
        self.cpp_info.libs = ["tldregistry"]
