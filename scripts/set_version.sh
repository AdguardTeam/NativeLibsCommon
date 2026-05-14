#!/bin/bash

# This script is called by the release workflow to perform any version-related
# updates. Since we now use git tags instead of conandata.yml for versioning,
# this script currently serves as a hook point for any version-related tasks.
#
# Usage: ./set_version.sh <version>
# Example: ./set_version.sh 8.2.0

set -e

VERSION=$1

if [ -z "$VERSION" ]; then
    echo "Usage: $0 <version>"
    echo "Example: $0 8.2.0"
    exit 1
fi

# Validate version format
if ! [[ "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "Error: Invalid version format. Expected X.Y.Z (e.g., 8.2.0)"
    exit 1
fi

echo "Setting version to $VERSION"

# The version is determined by git tags, so no file updates needed.
# This script serves as a hook point for any future version-related tasks.

echo "Version $VERSION is ready for release."
echo "The tag will be created automatically when the PR is merged."
