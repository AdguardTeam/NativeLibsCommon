from conan import ConanFile
from conan.errors import ConanInvalidConfiguration
from conan.tools.files import patch, copy, replace_in_file
from os import environ
from os.path import join


class QuicheConan(ConanFile):
    name = "quiche"
    version = "0.17.1"
    settings = "os", "compiler", "build_type", "arch"
    requires = ["openssl/boring-2024-09-13@adguard/oss"]
    exports_sources = ["CMakeLists.txt", "patches/*"]

    def source(self):
        self.run("git clone https://github.com/cloudflare/quiche.git source_subfolder")
        self.run(f"cd source_subfolder && git checkout {self.version}")
        patch(self, base_path="source_subfolder", patch_file="patches/crate_type.patch")
        patch(self, base_path="source_subfolder", patch_file="patches/ssize_t.patch")

    def _detect_ndk_from_compiler(self):
        """Detect NDK path from C compiler path."""
        try:
            compilers_from_conf = self.conf.get("tools.build:compiler_executables", default={}, check_type=dict)
            if 'c' in compilers_from_conf:
                cc_path = compilers_from_conf['c']
                # NDK compiler path looks like: /path/to/ndk/25.2.9519653/toolchains/llvm/prebuilt/darwin-x86_64/bin/clang
                # or: /path/to/android-ndk-r25c/toolchains/...
                # Find 'ndk/' or 'android-ndk' and take path up to next component
                for marker in ('/ndk/', '/android-ndk'):
                    idx = cc_path.find(marker)
                    if idx != -1:
                        # Find end of NDK version component (next '/' after marker)
                        end = cc_path.find('/', idx + len(marker))
                        return cc_path[:end] if end != -1 else cc_path
        except Exception as e:
            self.output.warning(f"Failed to detect NDK from compiler: {e}")
        return None

    def build(self):
        environ["RUSTFLAGS"] = "%s -C relocation-model=pic" \
                               % (environ["RUSTFLAGS"] if "RUSTFLAGS" in environ else "")

        cargo_build_type = "--release" if self.settings.build_type != "Debug" else ""

        os = self.settings.os
        arch = str(self.settings.arch)
        openssl_path = self.dependencies["openssl"].package_folder.replace("\\", "/")
        if os == "Linux":
            replace_in_file(self, join(self.source_folder, "source_subfolder/quiche", "Cargo.toml"), "ring = \"0.16\"", "ring = \"0.17\"")

            if arch == "armv8":
                arch = "aarch64"
            elif arch == "x86":
                arch = "i686"

            compilers_from_conf = self.conf.get("tools.build:compiler_executables", default={}, check_type=dict)
            musl = "musl" in compilers_from_conf['c']
            eabi = "eabi" if (arch == "arm" or arch == "armv7") else ""
            sf = "sf" if (arch == "mips" or arch == "mipsel") else ""
            cargo_args = "build %s --target %s-unknown-linux-%s%s" % (cargo_build_type, arch, "musl" if musl else "gnu", eabi)
            environ["CROSS_COMPILE"] = ("%s-linux-musl%s%s-" % (arch, eabi, sf)) if musl else ("%s-unknown-linux-gnu-" % (arch))
        elif os == "Android":
            if "ANDROID_NDK_HOME" not in environ:
                # Try to detect NDK from C compiler path
                ndk_path = self._detect_ndk_from_compiler()
                if ndk_path:
                    self.output.info(f"Detected NDK path from compiler: {ndk_path}")
                    environ["ANDROID_NDK_HOME"] = ndk_path
                elif "ANDROID_HOME" in environ:
                    environ["ANDROID_NDK_HOME"] = "%s/ndk-bundle" % environ["ANDROID_HOME"]

            platform = "21"

            if arch == "armv7":
                target = "armv7-linux-androideabi"
            elif arch == "armv8":
                target = "aarch64-linux-android"
            elif arch == "x86":
                target = "i686-linux-android"
            elif arch == "x86_64":
                target = "x86_64-linux-android"
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
            if arch == "x86_64":
                target = "x86_64-pc-windows-msvc"
            elif arch == "armv8":
                target = "aarch64-pc-windows-msvc"
                replace_in_file(self, join(self.source_folder, "source_subfolder/quiche", "Cargo.toml"), "ring = \"0.16\"", "ring = \"0.17\"")
                environ["PATH"] = f"{environ['PATH']};C:\\Program Files\\LLVM\\bin;C:\\Program Files (x86)\\LLVM\\bin"
            else:
                target = "i686-pc-windows-msvc"
            environ["RUSTFLAGS"] = "%s -C target-feature=+crt-static" % environ["RUSTFLAGS"]
            cargo_args = "build %s --target %s" % (cargo_build_type, target)
        else:
            raise ConanInvalidConfiguration("Unsupported OS: %s" % os)

        environ["QUICHE_BSSL_PATH"] = "%s/lib" % openssl_path
        cargo_quiche_features = "--no-default-features --features ffi"
        cargo_args = "%s %s" % (cargo_args, cargo_quiche_features)
        self.run("cd source_subfolder/quiche && cargo %s" % (cargo_args))

    def package(self):
        copy(self, "*.h", src=join(self.source_folder, "source_subfolder/quiche/include"), dst=join(self.package_folder, "include"), keep_path = True)
        copy(self, "*.lib", src=self.build_folder, dst=join(self.package_folder, "lib"), keep_path=False)
        copy(self, "*.a", src=self.build_folder, dst=join(self.package_folder, "lib"), keep_path=False)

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
