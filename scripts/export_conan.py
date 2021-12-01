#!/usr/bin/env python

import os

work_dir = os.path.dirname(os.path.realpath(__file__))
project_dir = os.path.dirname(work_dir)
recipes_dir = os.path.join(project_dir, 'conan', 'recipes')

for folder in os.listdir(recipes_dir):
    if os.path.isdir(folder):
        os.system("conan export %s AdguardTeam/NativeLibsCommon" % folder)

os.system("conan export ../conan AdguardTeam/NativeLibsCommon")
