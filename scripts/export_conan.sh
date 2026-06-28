#!/bin/sh

# Export native_libs_common (at the current `git describe` version) and all
# custom Conan recipes from `conan/recipes/` to the local Conan cache.
#
# The package is exported at the version reported by `git describe` (e.g.
# `8.1.37` on a tag, or `8.1.37-1-g126630b` in between); when it is built, the
# recipe checks out the matching commit. To build uncommitted working-tree
# changes instead, use `conan create . --version local`.

set -e

cd "$(dirname "$0")/.."

version=$(git describe --tags --match "v*" | sed 's/^v//')
conan export . --user adguard --channel oss --version "$version"

for recipe in conan/recipes/*/; do
    conan export "$recipe" --user adguard --channel oss
done
