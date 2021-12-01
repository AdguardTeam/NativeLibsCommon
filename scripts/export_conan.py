#!/usr/bin/env python

import os

recipes_dir = "./conan/recipes"

for folder in os.listdir(recipes_dir):
    if os.path.isdir(folder):
        os.system("conan export %s AdguardTeam/NativeLibsCommon" % folder)

os.system("conan export ../conan AdguardTeam/NativeLibsCommon")
