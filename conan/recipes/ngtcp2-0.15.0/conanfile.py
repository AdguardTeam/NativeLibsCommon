from conan import ConanFile
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout
from conan.tools.files import patch


class Ngtcp2Conan(ConanFile):
    name = "ngtcp2"
    version = "0.15.0"
    settings = "os", "compiler", "build_type", "arch"
    options = {"shared": [True, False], "fPIC": [True, False]}
    default_options = {"shared": False, "fPIC": True}
    requires = ["openssl/boring-2021-05-11@AdguardTeam/NativeLibsCommon"]
    exports_sources = ["CMakeLists.txt", "patches/popcnt_old_cpu_fix.patch"]

    def config_options(self):
        if self.settings.os == "Windows":
            del self.options.fPIC

    def source(self):
        self.run("git clone https://github.com/ngtcp2/ngtcp2.git source_subfolder")
        self.run("cd source_subfolder && git checkout v0.15.0")
        patch(self, base_path="source_subfolder", patch_file="patches/popcnt_old_cpu_fix.patch")

    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()
        tc = CMakeToolchain(self)
        tc.variables["ENABLE_SHARED_LIB"] = "OFF"
        tc.variables["ENABLE_OPENSSL"] = "OFF"
        tc.variables["ENABLE_BORINGSSL"] = "ON"
        tc.variables["HAVE_SSL_IS_QUIC"] = "ON"
        tc.variables["HAVE_SSL_SET_QUIC_EARLY_DATA_CONTEXT"] = "ON"
        tc.generate()

    def layout(self):
        cmake_layout(self)

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

        # Explicit way:
        # self.run('cmake %s/hello %s'
        #          % (self.source_folder, cmake.command_line))
        # self.run("cmake --build . %s" % cmake.build_config)

    def package(self):
        self.copy("*.h", dst="include", src="source_subfolder/lib/includes")
        self.copy("*.h", dst="include", src="source_subfolder/crypto/includes")
        self.copy("*.lib", dst="lib", keep_path=False)
        self.copy("*.dll", dst="bin", keep_path=False)
        self.copy("*.so", dst="lib", keep_path=False)
        self.copy("*.dylib", dst="lib", keep_path=False)
        self.copy("*.a", dst="lib", keep_path=False)

    def package_info(self):
        self.cpp_info.libs = ["ngtcp2", "ngtcp2_crypto_boringssl"]
        self.cpp_info.defines.append("NGTCP2_STATICLIB=1")
