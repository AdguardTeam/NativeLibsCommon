#!/usr/bin/env python3

import os
import subprocess
import yaml

work_dir = os.path.dirname(os.path.realpath(__file__))
project_dir = os.path.dirname(work_dir)
recipes_dir = os.path.join(project_dir, 'conan', 'recipes')
global yaml_data

versions = ["777"]
conandata_path = os.path.join(project_dir, "conandata.yml")
if os.path.exists(conandata_path):
    yaml_data = yaml.safe_load(open(conandata_path, "r"))
    for version in yaml_data["commit_hash"]:
         versions.append(version)

for folder in os.listdir(recipes_dir):
    path = os.path.join(recipes_dir, folder)
    if os.path.isdir(path):
        subprocess.run(["conan", "export", path, "AdguardTeam/NativeLibsCommon"])

for version in versions:
    if (version == "777"):
        subprocess.run(["git", "checkout", "master"])
    else:
        hash1 = yaml_data["commit_hash"][version]["hash"]
        result = subprocess.run(["git", "log", "--reverse", "--ancestry-path", hash1 + "..master", "--pretty=%h"], capture_output=True)
        the_hash = result.stdout.decode().splitlines()[0]
        print("HASH is ", the_hash)
        subprocess.run(["git", "checkout", the_hash])
    subprocess.run(["conan", "export", project_dir, "/" + version + "@AdguardTeam/NativeLibsCommon"])