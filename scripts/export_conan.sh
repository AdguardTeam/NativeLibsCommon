#!/bin/sh

for RECIPE_DIR in $(find /../conan/recipes -type d -maxdepth 1 -mindepth 1); do
    conan export "$RECIPE_DIR" AdguardTeam/NativeLibsCommon
done

conan /../common AdguardTeam/NativeLibsCommon