#!/usr/bin/env bash

set -e -x

# Get Chromium sources
curl -s https://raw.githubusercontent.com/chromium/chromium/main/net/tools/dafsa/make_dafsa.py \
    -o chromium/net/tools/dafsa/make_dafsa.py --create-dirs
curl -s https://raw.githubusercontent.com/chromium/chromium/main/net/base/registry_controlled_domains/effective_tld_names.gperf \
    -o chromium/net/base/registry_controlled_domains/effective_tld_names.gperf --create-dirs
curl -s https://raw.githubusercontent.com/chromium/chromium/main/net/base/lookup_string_in_fixed_set.h \
    -o chromium/net/base/lookup_string_in_fixed_set.h --create-dirs
curl -s https://raw.githubusercontent.com/chromium/chromium/main/net/base/lookup_string_in_fixed_set.cc \
    -o chromium/net/base/lookup_string_in_fixed_set.cc --create-dirs

# Patch Chromium sources
git apply -v patches/make_dafsa.patch
git apply -v patches/lookup_string_in_fixed_set.patch

# Make DAFSA
python3 chromium/net/tools/dafsa/make_dafsa.py \
    chromium/net/base/registry_controlled_domains/effective_tld_names.gperf \
    src/effective_tld_names.inc

# Update the version number
CMD=sed
if [[ "$(uname)" =~ "Darwin" ]]; then
  CMD=gsed
fi
${CMD} -i"" -e "s/^    version = \"\(.*\)\"$/    version = \"$(date +%Y-%m-%d)\"/" conanfile.py
