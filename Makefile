# Builds are driven by the CMake presets in CMakePresets.json. The active
# preset is selected from COMPILER (clang/msvc) and BUILD_TYPE (release/debug),
# but PRESET can be set directly to use any preset, e.g.
#   make PRESET=clang-debug-sanitizer test
#   make PRESET=musl-cross-x86_64-relwithdebinfo build_libs
BUILD_TYPE ?= release

ifeq ($(OS), Windows_NT)
COMPILER ?= msvc
else
COMPILER ?= clang
endif

ifeq ($(BUILD_TYPE), release)
PRESET ?= $(COMPILER)-relwithdebinfo
else
PRESET ?= $(COMPILER)-debug
endif

# Each preset configures into ${sourceDir}/cmake-build-${presetName}.
BUILD_DIR = cmake-build-$(PRESET)
COMPILE_COMMANDS = $(BUILD_DIR)/compile_commands.json

ifeq ($(OS), Windows_NT)
NPROC ?= $(or $(NUMBER_OF_PROCESSORS),8)
else
NPROC ?= $(shell (nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 8) | tr -d '\n')
UNAME_S := $(shell uname -s)
endif

# On macOS CMake would otherwise build for whatever architecture the toolchain
# defaults to, so pin it to the host. Override with ARCH, which also takes a
# semicolon-separated list for a universal binary, e.g.
#   make ARCH=x86_64 test
#   make ARCH='arm64;x86_64' build_libs
# Not applied to the cross-compiling presets, which don't target Apple.
ifeq ($(UNAME_S), Darwin)
ifeq ($(findstring cross,$(PRESET)),)
ARCH ?= $(shell uname -m)
OSX_ARCH_ARGS = -DCMAKE_OSX_ARCHITECTURES="$(ARCH)"
endif
endif

.PHONY: all
## Build the libraries (default target).
all: build_libs

.PHONY: export_conan
## Export the custom Conan recipes (conan/recipes) to the local Conan cache.
## Needed for offline/local builds when the AdGuard Conan remote is unavailable.
export_conan:
	./scripts/export_conan.sh

.PHONY: setup_cmake
## Configure the project with the selected CMake preset (resolves Conan deps).
## Extra CMake flags can be passed via CMAKE_ARGS, e.g.
##   make CMAKE_ARGS=-DCMAKE_OSX_ARCHITECTURES=arm64 test
setup_cmake:
	cmake --preset $(PRESET) $(OSX_ARCH_ARGS) $(CMAKE_ARGS)

.PHONY: compile_commands
## Generate compile_commands.json for IDE / clang-tidy integration.
compile_commands:
	cmake --preset $(PRESET) $(OSX_ARCH_ARGS) $(CMAKE_ARGS) -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

.PHONY: build_libs
## Build all libraries (ag_common, ag_common_http, ag_common_tls).
build_libs: setup_cmake
	cmake --build $(BUILD_DIR) -j$(NPROC)

.PHONY: clean
## Clean build artifacts.
clean:
	cmake --build $(BUILD_DIR) --target clean

.PHONY: test
test: test-cpp

.PHONY: test-cpp
## Build and run all unit tests via CTest.
## A JUnit report is written to $(BUILD_DIR)/junit.xml for CI consumption.
test-cpp: setup_cmake
	cmake --build $(BUILD_DIR) --target tests -j$(NPROC)
	ctest --test-dir $(BUILD_DIR) --output-on-failure --output-junit junit.xml

.PHONY: lint
## Run all linters.
lint: lint-md lint-cpp

.PHONY: lint-cpp
## Lint C++ files (formatting + static analysis).
lint-cpp: clang-format clangd-tidy

.PHONY: clang-format
## Check C++ formatting with clang-format.
clang-format:
	git ls-files --exclude-standard -- 'common/*' 'http/*' 'tls/*' \
		| grep -E '\.(cpp|c|h)$$' \
		| xargs clang-format -n -Werror

.PHONY: clangd-tidy
## Run clangd-tidy static analysis (much faster than run-clang-tidy).
## Installs clangd-tidy and tqdm into a local `.venv` from requirements.txt.
## Set SKIP_VENV=1 to use clangd-tidy already available on PATH.
clangd-tidy: compile_commands
ifeq ($(SKIP_VENV),1)
	jq -r '.[] | select(.file | endswith(".cpp")) | .file' $(COMPILE_COMMANDS) \
		| grep -vE '(^|/)third-party(/|$$)' \
		| sort -u \
		| xargs clangd-tidy -p $(BUILD_DIR) --fail-on-severity error --tqdm -j$(NPROC)
else
	python3 -m venv .venv && \
	. .venv/bin/activate && \
	pip install -r requirements.txt && \
	jq -r '.[] | select(.file | endswith(".cpp")) | .file' $(COMPILE_COMMANDS) \
		| grep -vE '(^|/)third-party(/|$$)' \
		| sort -u \
		| xargs clangd-tidy -p $(BUILD_DIR) --fail-on-severity error --tqdm -j$(NPROC)
endif

.PHONY: lint-md
## Lint Markdown files with markdownlint.
lint-md:
	npx -y markdownlint-cli2 .

.PHONY: lint-fix
## Auto-fix fixable linter issues.
lint-fix: lint-fix-cpp lint-fix-md

.PHONY: lint-fix-cpp
## Auto-fix C++ formatting with clang-format.
lint-fix-cpp:
	git ls-files --exclude-standard -- 'common/*' 'http/*' 'tls/*' \
		| grep -E '\.(cpp|c|h)$$' \
		| xargs clang-format -i

.PHONY: lint-fix-md
## Auto-fix Markdown files with markdownlint.
lint-fix-md:
	npx -y markdownlint-cli2 --fix .

.PHONY: list-deps-dirs
## List the Conan dependency package directories (for finding headers).
list-deps-dirs: compile_commands
	@GENERATORS_DIR=$$(cmake -L $(BUILD_DIR) 2>/dev/null \
		| grep '_DIR:PATH=' \
		| grep -v 'NOTFOUND' \
		| head -1 \
		| sed 's/.*:PATH=//') && \
	grep -h '_PACKAGE_FOLDER_' "$$GENERATORS_DIR"/* 2>/dev/null \
		| sed -n 's/set(\(.*\)_PACKAGE_FOLDER_[A-Z_]* "\([^"]*\)").*/\1 \2/p' \
		| sort -u
