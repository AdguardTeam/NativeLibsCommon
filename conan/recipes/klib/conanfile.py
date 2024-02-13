from conan import ConanFile
from conan.tools.files import copy
from os.path import join


class KlibConan(ConanFile):
    name = "klib"
    version = "2021-04-06"
    settings = "os", "compiler", "build_type", "arch"
    options = {"shared": [True, False], "fPIC": [True, False]}
    default_options = {"shared": False, "fPIC": True}

    def config_options(self):
        if self.settings.os == "Windows":
            del self.options.fPIC

    def source(self):
        self.run("git clone https://github.com/attractivechaos/klib")
        self.run("cd klib && git checkout e1b2a40de8e2a46c05cc5dac9c6e5e8d15ae722c")

    def package(self):
        copy(self, "khash.h", src=join(self.source_folder, "klib"), dst=join(self.package_folder, "include"), keep_path = True)
        copy(self, "kvec.h", src=join(self.source_folder, "klib"), dst=join(self.package_folder, "include"), keep_path = True)
