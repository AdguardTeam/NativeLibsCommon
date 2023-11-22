from conans import ConanFile, tools
from os import environ


class QuicheConan(ConanFile):
    name = "quiche"
    version = "0.17.1"
    settings = "os", "compiler", "build_type", "arch"
    generators = "cmake"
    requires = ["openssl/boring-2021-05-11@AdguardTeam/NativeLibsCommon"]
    exports_sources = ["CMakeLists.txt", "patches/*"]

    def config_options(self):
        if self.settings.os == "Windows":
            del self.options.fPIC

    def source(self):
        self.run("git clone https://github.com/cloudflare/quiche.git source_subfolder")
        self.run(f"cd source_subfolder && git checkout {self.version}")
        tools.patch(base_path="source_subfolder", patch_file="patches/crate_type.patch")
        tools.patch(base_path="source_subfolder", patch_file="patches/ssize_t.patch")

    def build(self):
        environ["RUSTFLAGS"] = "%s -C relocation-model=pic" \
                               % (environ["RUSTFLAGS"] if "RUSTFLAGS" in environ else "")

        cargo_build_type = "--release" if self.settings.build_type != "Debug" else ""

        os = self.settings.os
        arch = str(self.settings.arch)
        if os == "Linux":
            if arch == "armv8":
                arch = "aarch64"
            elif arch == "x86":
                arch = "i686"

            cargo_args = "build %s --target %s-unknown-linux-gnu" % (cargo_build_type, arch)
        elif os == "Android":
            if "ANDROID_HOME" in environ and "ANDROID_NDK_HOME" not in environ:
                environ["ANDROID_NDK_HOME"] = "%s/ndk-bundle" % environ["ANDROID_HOME"]

            if arch == "armv7":
                target = "armv7-linux-androideabi"
                platform = "19"
            elif arch == "armv8":
                target = "aarch64-linux-android"
                platform = "21"
            elif arch == "x86":
                target = "i686-linux-android"
                platform = "19"
            elif arch == "x86_64":
                target = "x86_64-linux-android"
                platform = "21"
            else:
                raise ConanInvalidConfiguration("Unsupported Android architecture: %s" % arch)

            cargo_args = "ndk --target %s --platform %s -- build %s" \
                         % (target, platform, cargo_build_type)
        elif os == "iOS":
            if os.sdk == "iphonesimulator":
                if arch.startswith("arm"):
                    target = "aarch64-apple-ios-sim"
                    cargo_args = "build %s"
                else:
                    target = "x86_64-apple-ios"
                    cargo_args = "build %s"
            elif os.sdk == "iphoneos":
                target = "aarch64-apple-ios"
                cargo_args = "build %s"
            else:
                raise ConanInvalidConfiguration("Unsupported iOS SDK: %s" % os.sdk)
            cargo_args = "%s --target %s" % (cargo_args % cargo_build_type, target)
        elif os == "Macos":
            if arch.startswith("arm"):
                target = "aarch64-apple-darwin"
            else:
                target = "x86_64-apple-darwin"
            cargo_args = "build %s --target %s" % (cargo_build_type, target)
        elif os == "Windows":
            environ["RUSTFLAGS"] = "%s -C target-feature=+crt-static" % environ["RUSTFLAGS"]
            cargo_args = "build %s --target i686-pc-windows-msvc" % cargo_build_type
        else:
            raise ConanInvalidConfiguration("Unsupported OS: %s" % os)

        cargo_quiche_features = "--no-default-features --features ffi"
        cargo_args = "%s %s" % (cargo_args, cargo_quiche_features)
        self.run("cd source_subfolder/quiche && cargo %s" % cargo_args)

    def package(self):
        self.copy("*.h", dst="include", src="source_subfolder/quiche/include")
        self.copy("*.lib", dst="lib", keep_path=False)
        self.copy("*.a", dst="lib", keep_path=False)

    def package_info(self):
        if self.settings.os == "Windows":
            self.cpp_info.system_libs = ["UserEnv"]
        else:
            self.cpp_info.exelinkflags = ["-ldl"]

        self.cpp_info.libs = ["quiche"]
        self.cpp_info.requires = [
            "openssl::ssl",
            "openssl::crypto",
        ]
