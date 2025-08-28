from conan import ConanFile
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout
from conan.tools.files import patch, copy
from conan.tools.apple import is_apple_os
from os.path import join
import os

class LibeventConan(ConanFile):
    name = "libevent"
    version = "2.1.11"
    settings = "os", "compiler", "build_type", "arch"
    options = {"shared": [True, False], "fPIC": [True, False]}
    default_options = {"shared": False, "fPIC": True}
    requires = ["openssl/boring-2024-09-13@adguard/oss"]
    exports_sources = ["CMakeLists.txt", "patches/*"]

    def config_options(self):
        if self.settings.os == "Windows":
            del self.options.fPIC

    def source(self):
        self.run("git clone https://github.com/libevent/libevent.git source_subfolder")
        self.run("cd source_subfolder && git checkout release-2.1.11-stable")
        # Apply all patches from the `patches` directory
        patches_path = os.path.join("patches")
        patches = sorted([f for f in os.listdir(patches_path) if os.path.isfile(os.path.join(patches_path, f))])
        for patch_name in patches:
            patch(self, base_path="source_subfolder", patch_file=os.path.join(patches_path, patch_name))

    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()
        tc = CMakeToolchain(self)
        if self.options.shared:
            tc.cache_variables["EVENT__LIBRARY_TYPE"] = "SHARED"
        else:
            tc.cache_variables["EVENT__LIBRARY_TYPE"] = "STATIC"

        if self.settings.os == "Android":
            tc.cache_variables["THREADS_PREFER_PTHREAD_FLAG"] = "ON"

        tc.cache_variables["OPENSSL_ROOT_DIR"] = self.dependencies["openssl"].package_folder.replace("\\", "/")

        tc.cache_variables["EVENT__DISABLE_TESTS"] = "ON"
        tc.cache_variables["EVENT__DISABLE_REGRESS"] = "ON"
        tc.cache_variables["EVENT__DISABLE_BENCHMARK"] = "ON"
        tc.cache_variables["EVENT__DISABLE_SAMPLES"] = "ON"

        # TODO: remove this after updating to version newer than 2.1.11
        tc.cache_variables["CMAKE_POLICY_VERSION_MINIMUM"] = "3.24"
        tc.generate()

    def layout(self):
        cmake_layout(self)

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        copy(self, "*.h", src=join(self.build_folder, "source_subfolder/include/event2"), dst=join(self.package_folder, "include/event2"))
        copy(self, "*.h", src=join(self.source_folder, "source_subfolder/include/event2"), dst=join(self.package_folder, "include/event2"))
        copy(self, "*.h", src=join(self.source_folder, "source_subfolder/include"), dst=join(self.package_folder, "include"))
        copy(self, "*.dll", src=self.build_folder, dst=join(self.package_folder, "bin"), keep_path=False)
        copy(self, "*.lib", src=self.build_folder, dst=join(self.package_folder, "lib"), keep_path=False)
        copy(self, "*.so", src=self.build_folder, dst=join(self.package_folder, "lib"), keep_path=False)
        copy(self, "*.dylib", src=self.build_folder, dst=join(self.package_folder, "lib"), keep_path=False)
        copy(self, "*.a", src=self.build_folder, dst=join(self.package_folder, "lib"), keep_path=False)

    def package_info(self):
        if self.settings.os == "Windows":
            self.cpp_info.system_libs = ["ws2_32"]
            self.cpp_info.libs = ["event", "event_core", "event_extra", "event_openssl"]
        else:
            self.cpp_info.libs = ["event", "event_core", "event_extra", "event_pthreads", "event_openssl"]
        if is_apple_os(self):
            self.cpp_info.frameworks = ['Foundation', 'SystemConfiguration']
