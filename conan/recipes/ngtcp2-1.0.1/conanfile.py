from conan import ConanFile
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout
from conan.tools.files import patch, copy
from os.path import join


class Ngtcp2Conan(ConanFile):
    name = "ngtcp2"
    version = "1.0.1"
    settings = "os", "compiler", "build_type", "arch"
    options = {"shared": [True, False], "fPIC": [True, False]}
    default_options = {"shared": False, "fPIC": True}
    requires = ["openssl/boring-2023-05-17@adguard_team/native_libs_common"]
    exports_sources = ["CMakeLists.txt", "patches/popcnt_old_cpu_fix.patch"]

    def config_options(self):
        if self.settings.os == "Windows":
            del self.options.fPIC

    def source(self):
        self.run("git clone https://github.com/ngtcp2/ngtcp2.git source_subfolder")
        self.run(f"cd source_subfolder && git checkout v{self.version}")
        patch(self, base_path="source_subfolder", patch_file="patches/popcnt_old_cpu_fix.patch")

    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()
        tc = CMakeToolchain(self)
        tc.cache_variables["OPENSSL_ROOT_DIR"] = self.dependencies["openssl"].package_folder.replace("\\", "/")
        tc.cache_variables["ENABLE_SHARED_LIB"] = "OFF"
        tc.cache_variables["ENABLE_OPENSSL"] = "boring" not in str(self.dependencies["openssl"].ref.version)
        tc.cache_variables["ENABLE_BORINGSSL"] = "boring" in str(self.dependencies["openssl"].ref.version)
        tc.cache_variables["HAVE_SSL_IS_QUIC"] = "ON"
        tc.cache_variables["HAVE_SSL_SET_QUIC_EARLY_DATA_CONTEXT"] = "ON"
        tc.generate()

    def layout(self):
        cmake_layout(self)

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        copy(self, "*.h", src=join(self.build_folder, "source_subfolder/lib/includes"), dst=join(self.package_folder, "include"), keep_path = True)
        copy(self, "*.h", src=join(self.source_folder, "source_subfolder/lib/includes"), dst=join(self.package_folder, "include"), keep_path = True)
        copy(self, "*.h", src=join(self.source_folder, "source_subfolder/crypto/includes"), dst=join(self.package_folder, "include"), keep_path = True)
        copy(self, "*.lib", self.build_folder, dst=join(self.package_folder, "lib"), keep_path=False)
        copy(self, "*.a", self.build_folder, dst=join(self.package_folder, "lib"), keep_path=False)
        copy(self, "*.dll", self.build_folder, dst=join(self.package_folder, "bin"), keep_path=False)
        copy(self, "*.so", self.build_folder, dst=join(self.package_folder, "lib"), keep_path=False)
        copy(self, "*.dylib", self.build_folder, dst=join(self.package_folder, "lib"), keep_path=False)

    def package_info(self):
        if "boring" in str(self.dependencies["openssl"].ref.version):
            self.cpp_info.libs = ["ngtcp2", "ngtcp2_crypto_boringssl"]
        else:
            self.cpp_info.libs = ["ngtcp2", "ngtcp2_crypto_quictls"]
        self.cpp_info.defines.append("NGTCP2_STATICLIB=1")
