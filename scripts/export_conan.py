#!/usr/bin/env python

import os

work_dir = os.path.dirname(os.path.realpath(__file__))
project_dir = os.path.dirname(work_dir)
recipes_dir = os.path.join(project_dir, 'conan', 'recipes')

for folder in os.listdir(recipes_dir):
    path = recipes_dir + '/' + folder
    if os.path.isdir(path):
        os.system("conan export %s AdguardTeam/NativeLibsCommon" % path)

os.system("conan export ../ AdguardTeam/NativeLibsCommon")
