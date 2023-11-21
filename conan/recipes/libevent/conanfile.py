from conan import ConanFile
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout
from conan.tools.files import patch

class LibeventConan(ConanFile):
    name = "libevent"
    version = "2.1.11"
    settings = "os", "compiler", "build_type", "arch"
    options = {"shared": [True, False], "fPIC": [True, False]}
    default_options = {"shared": False, "fPIC": True}
    requires = ["openssl/boring-2021-05-11@AdguardTeam/NativeLibsCommon"]
    exports_sources = ["CMakeLists.txt", "patches/*"]

    def config_options(self):
        if self.settings.os == "Windows":
            del self.options.fPIC

    def source(self):
        self.run("git clone https://github.com/libevent/libevent.git source_subfolder")
        self.run("cd source_subfolder && git checkout release-2.1.11-stable")
        patch(self, base_path="source_subfolder", patch_file="patches/0001-Maximum-evbuffer-read-configuration.patch")
        patch(self, base_path="source_subfolder", patch_file="patches/bufferevent-prepare-fd.patch")
        patch(self, base_path="source_subfolder", patch_file="patches/bufferevent-socket-connect-error.patch")
        patch(self, base_path="source_subfolder", patch_file="patches/evutil_socket_error_to_string_lang.patch")
        patch(self, base_path="source_subfolder", patch_file="patches/reinit_notifyfds.patch")
        patch(self, base_path="source_subfolder", patch_file="patches/win32-disable-evsig.patch")
        patch(self, base_path="source_subfolder", patch_file="patches/linux-disable-sysctl.patch")

    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()
        tc = CMakeToolchain(self)
        if self.options.shared:
            tc.variables["EVENT__LIBRARY_TYPE"] = "SHARED"
        else:
            tc.variables["EVENT__LIBRARY_TYPE"] = "STATIC"
        tc.variables["EVENT__DISABLE_TESTS"] = "ON"
        tc.variables["EVENT__DISABLE_REGRESS"] = "ON"
        tc.variables["EVENT__DISABLE_BENCHMARK"] = "ON"
        tc.variables["EVENT__DISABLE_SAMPLES"] = "ON"
        tc.generate()

    def layout(self):
        cmake_layout(self)

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        # self.run('cmake %s %s || cmake %s %s'
        #          % (self.source_folder, cmake.command_line, self.source_folder, cmake.command_line))
        cmake.build()

    def package(self):
        self.copy("*.h", dst="include/event2", src="include/event2")
        self.copy("*.h", dst="include/event2", src="source_subfolder/include/event2")
        self.copy("*.h", dst="include", src="source_subfolder/include")
        self.copy("*.dll", dst="bin", keep_path=False)
        self.copy("*.lib", dst="lib", keep_path=False)
        self.copy("*.so", dst="lib", keep_path=False)
        self.copy("*.dylib", dst="lib", keep_path=False)
        self.copy("*.a", dst="lib", keep_path=False)

    def package_info(self):
        if self.settings.os == "Windows":
            self.cpp_info.system_libs = ["ws2_32"]
            self.cpp_info.libs = ["event", "event_core", "event_extra", "event_openssl"]
        else:
            self.cpp_info.libs = ["event", "event_core", "event_extra", "event_pthreads", "event_openssl"]
