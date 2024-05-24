import os
import shutil
import subprocess

def modify_conanfile():
    shutil.copyfile('conanfile.py', 'conanfile_backup.py')

    with open('conanfile.py', 'r') as file:
        lines = file.readlines()

    new_lines = []
    source_found = False
    for line in lines:
        if line.strip().startswith('def source(self):'):
            source_found = True
            continue
        if source_found:
            if line.startswith('    '):
                continue
            else:
                source_found = False
        if line.strip().startswith('exports_sources'):
            new_lines.append('    exports_sources = ["*"]\n')
            continue
        new_lines.append(line)

    with open('conanfile.py', 'w') as file:
        file.writelines(new_lines)

def export_conan_package():
    subprocess.run(["conan", "export", ".", "--version", "local"], check=True)

    shutil.move('conanfile_backup.py', 'conanfile.py')

if __name__ == "__main__":
    modify_conanfile()
    export_conan_package()
