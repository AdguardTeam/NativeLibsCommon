from conan import ConanFile
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout
from conan.tools.files import patch, copy, get
from os.path import join
import os


class Ngtcp2Conan(ConanFile):
    name = "ngtcp2"
    version = "1.22.1"
    settings = "os", "compiler", "build_type", "arch"
    options = {"shared": [True, False], "fPIC": [True, False]}
    default_options = {"shared": False, "fPIC": True}
    requires = ["openssl/boring-2024-09-13@adguard/oss"]
    exports_sources = ["CMakeLists.txt", "patches/*"]

    def config_options(self):
        if self.settings.os == "Windows":
            del self.options.fPIC

    def source(self):
        get(
            self,
            f"https://github.com/ngtcp2/ngtcp2/releases/download/v{self.version}/ngtcp2-{self.version}.tar.gz",
            sha256="063d80531acac0ddbbc1b9d12829a824edc2abe8dba2e632fd1ce15cfd5632f9",
            destination="source_subfolder",
            strip_root=True,
        )
        # Apply all patches from the `patches` directory
        patches_path = os.path.join("patches")
        patches = sorted([f for f in os.listdir(patches_path) if os.path.isfile(os.path.join(patches_path, f))])
        for patch_name in patches:
            patch(self, base_path="source_subfolder", patch_file=os.path.join(patches_path, patch_name))

    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()
        tc = CMakeToolchain(self)

        openssl_dep = self.dependencies["openssl"]
        pkg = openssl_dep.package_folder.replace("\\", "/")
        is_boring = "boring" in str(openssl_dep.ref.version)

        tc.cache_variables["ENABLE_SHARED_LIB"] = "OFF"
        tc.cache_variables["ENABLE_LIB_ONLY"] = "ON"
        tc.cache_variables["BUILD_TESTING"] = "OFF"
        tc.cache_variables["ENABLE_OPENSSL"] = not is_boring
        tc.cache_variables["ENABLE_BORINGSSL"] = is_boring

        if is_boring:
            # ngtcp2 v1.22.1 has no FindBoringSSL.cmake — pass paths explicitly.
            # Used by cmake's check_symbol_exists during configure.
            tc.cache_variables["BORINGSSL_INCLUDE_DIR"] = f"{pkg}/include"
            lib_prefix = "" if str(self.settings.os) == "Windows" else "lib"
            lib_suffix = ".lib" if str(self.settings.os) == "Windows" else ".a"
            libs = [
                f"{pkg}/lib/{lib_prefix}{lib}{lib_suffix}"
                for comp in openssl_dep.cpp_info.components.values()
                for lib in comp.libs
            ]
            tc.cache_variables["BORINGSSL_LIBRARIES"] = ";".join(libs)
        else:
            tc.cache_variables["OPENSSL_ROOT_DIR"] = pkg

        tc.generate()

    def layout(self):
        cmake_layout(self)

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        copy(self, "*.h", src=join(self.build_folder, "source_subfolder/lib/includes"), dst=join(self.package_folder, "include"), keep_path=True)
        copy(self, "*.h", src=join(self.source_folder, "source_subfolder/lib/includes"), dst=join(self.package_folder, "include"), keep_path=True)
        copy(self, "*.h", src=join(self.source_folder, "source_subfolder/crypto/includes"), dst=join(self.package_folder, "include"), keep_path=True)
        copy(self, "*.lib", self.build_folder, dst=join(self.package_folder, "lib"), keep_path=False)
        copy(self, "*.a", self.build_folder, dst=join(self.package_folder, "lib"), keep_path=False)
        copy(self, "*.dll", self.build_folder, dst=join(self.package_folder, "bin"), keep_path=False)
        copy(self, "*.so", self.build_folder, dst=join(self.package_folder, "lib"), keep_path=False)
        copy(self, "*.dylib", self.build_folder, dst=join(self.package_folder, "lib"), keep_path=False)

    def package_info(self):
        if "boring" in str(self.dependencies["openssl"].ref.version):
            self.cpp_info.libs = ["ngtcp2_crypto_boringssl", "ngtcp2"]
        else:
            self.cpp_info.libs = ["ngtcp2_crypto_quictls", "ngtcp2"]
        self.cpp_info.defines.append("NGTCP2_STATICLIB=1")
