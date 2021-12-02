#!/usr/bin/env python3

import os
import subprocess

work_dir = os.path.dirname(os.path.realpath(__file__))
project_dir = os.path.dirname(work_dir)
recipes_dir = os.path.join(project_dir, 'conan', 'recipes')

for folder in os.listdir(recipes_dir):
    path = os.path.join(recipes_dir, folder)
    if os.path.isdir(path):
        subprocess.run(["conan", "export", path, "AdguardTeam/NativeLibsCommon"])

subprocess.run(["conan", "export", project_dir, "AdguardTeam/NativeLibsCommon"])