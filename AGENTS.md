# AGENTS.md — Project Guide for AI Coding Agents

## Project Overview

This is **NativeLibsCommon** (`native_libs_common`), AdGuard's shared C++ library
that underpins the company's open-source C++ projects (e.g. the DNS libraries). It
provides common utilities (logging, error handling, networking, string/URL helpers,
coroutines), an HTTP/1–HTTP/3 stack, and TLS certificate utilities. The library
builds on Linux, macOS, Windows, and Android.

See [README.md](README.md) for build and Conan usage details, and
[DEV_DOCS.md](DEV_DOCS.md) for additional developer documentation.

## Tech Stack

- **C++20** (primary), **C11** — core libraries
- **Kotlin / Gradle** — Android platform adapter (`android/`)
- **CMake 3.24+** — build system
- **Conan 2.0.4+** — C++ package manager
- **Ninja** — build backend
- **Clang 8+ / libc++** (preferred) or **GCC 9+** or **MSVC** — compilers

## Directory Structure

| Directory | Purpose |
| --- | --- |
| `common/` | Core utilities: logger, error, coroutines, sockets, URL, regex, network monitor → library `ag_common` |
| `http/` | HTTP/1, HTTP/2, HTTP/3 stack and header handling → library `ag_common_http` |
| `tls/` | TLS certificate utilities → library `ag_common_tls` |
| `<module>/include/common/` | Public headers, included as `common/<...>` |
| `<module>/test/` | Unit tests for the module |
| `android/` | Android (Kotlin/Gradle) adapter and bindings |
| `conan/recipes/` | Custom Conan recipes for AdGuard-patched dependencies |
| `conan/profiles/` | Conan profiles & CMake toolchains (linux, apple, android, windows, musl) |
| `cmake/` | CMake modules: `add_unit_test`, Conan bootstrap/provider |
| `scripts/` | Conan export helpers, version setting, changelog tooling |
| `.github/workflows/` | CI/CD pipelines (tests, release, mirror) |

### Module Dependency Flow

```text
ag_common ← ag_common_http
ag_common (independent) ag_common_tls
```

- `ag_common` links `fmt`, `libevent`, `pcre2` (plus platform system libs).
- `ag_common_http` links `ag_common` and the nghttp2/nghttp3/ngtcp2 stack.
- `ag_common_tls` links `openssl` (BoringSSL).

## Build Commands

Dependencies are resolved automatically by the cmake-conan provider
([cmake/conan_provider.cmake](cmake/conan_provider.cmake)) during CMake configure.
For offline builds, or when the AdGuard Conan remote is not configured, run
`make export_conan` first to export the custom recipes to the local Conan cache.

| Command | What It Does |
| --- | --- |
| `make` / `make build_libs` | CMake configure (resolves Conan deps) → build all libraries |
| `make test` | Build the `tests` target → run `ctest` |
| `make export_conan` | Export custom Conan recipes (`conan/recipes`) to the local cache |
| `make compile_commands` | Generate `compile_commands.json` for IDE / clang-tidy |
| `make lint` | Run all linters (`lint-cpp` + `lint-md`) |
| `make lint-cpp` | `clang-format` check + `clangd-tidy` |
| `make lint-md` | `markdownlint .` |
| `make lint-fix` | Auto-fix fixable linter issues |
| `make list-deps-dirs` | List Conan dependency package directories |
| `make clean` | Clean build artifacts |

Builds are driven by the CMake presets in
[CMakePresets.json](CMakePresets.json). The Makefile selects a preset from
`COMPILER` (default `clang`, or `msvc` on Windows) and `BUILD_TYPE`
(default `release` → `RelWithDebInfo`; `debug` → `Debug`), and configures into
`cmake-build-<preset>`. Override `PRESET` to use any preset directly:

- `make PRESET=clang-debug-sanitizer test`
- `make PRESET=musl-cross-x86_64-relwithdebinfo build_libs`

Available presets: `clang-relwithdebinfo`, `clang-debug`,
`clang-debug-sanitizer`, `msvc-relwithdebinfo`, `msvc-debug`, and the
zig-based musl cross presets `musl-cross-<arch>-<relwithdebinfo|debug>`
for `arch` in `x86_64`, `aarch64`, `arm`, `mips`, `mipsel`.

## Code Style

Full rules are documented in [README.md](README.md#code-style). Key points:

### General

- 4-space indentation, 120-column limit (see [.clang-format](.clang-format))
- LLVM-based formatting; binary operators break before the operator
- Operator block opens on the same line (`int main(...) {`, `if (...) {`);
  single-line branches still use braces
- All public/large methods documented in Doxygen (`/** */`) with `autobrief`
  (no explicit `@brief`); use `@return`, not `@returns`
- New headers use `#pragma once`
- Identifier prefixes other than `m_`, `g_`, `p_`, `_` are prohibited;
  avoid `p` unless truly needed

### C++

- Language standard: **C++20** (`-std=c++20`); use `libc++`
- Root namespace is `ag::`, max depth 2 (plus optional `::test`);
  namespaces are `snake_case`
- Classes / typedefs / `using` aliases: `CamelCase`
  (a `snake_case`-pointer alias may be `snake_case_ptr`)
- Variables, functions, methods, parameters: `snake_case`
- Member variables: `m_snake_case`; public struct members: `snake_case`
- Constants, `constexpr`, enum values, defines: `UPPER_CASE`
- Global variables: `g_snake_case`
- Includes order (Chromium-style): external `<...>`, then public `"..."`
  (alphabetical), then current-directory `"..."` (alphabetical; not in public headers)
- Static analysis via [.clang-tidy](.clang-tidy); all warnings are errors

### C

- No new C code.

### Markdown

- Linted with `markdownlint`
- Unordered lists use dashes (`-`)
- **Markdown table formatting (MD060)**: When the Markdownlint MD060 rule
  triggers, switch to tight table formatting with spaces. Example:

  ```markdown
  | Column1 | Column2 |
  | --- | --- |
  | Value 1 | Value 2 |
  ```

  Do NOT use extra padding or alignment characters beyond single spaces.

## Dependencies

Managed via Conan ([conanfile.py](conanfile.py)). Key libraries:

- **fmt** — formatting (built with `FMT_EXCEPTIONS=0`)
- **libevent** — async event loop
- **llhttp** — HTTP/1 parser
- **nghttp2 / nghttp3 / ngtcp2** — HTTP/2, HTTP/3, QUIC
- **openssl** (BoringSSL; OpenSSL-QUIC on MIPS) — TLS
- **pcre2** — regular expressions
- **magic_enum** — enum reflection
- **gtest** — unit testing (test-only)

AdGuard-patched packages (`@adguard/oss`) come from the AdGuard Conan remote or
from `conan/recipes/` via `make export_conan`. To find dependency headers
(e.g. when resolving includes), run `make list-deps-dirs` and look in each
directory's `include/` subdirectory.

## Mandatory Task Rules

You MUST follow these rules for EVERY task:

- You MUST verify your changes with the build, tests, and linters:
    - `make` — check that the code builds
    - `make test` — build and run unit tests
    - `make lint` — run the linters
    - `make lint-fix` — auto-fix fixable linting issues
    - `make clang-format` — check formatting only

- You MUST add or update unit tests (under the module's `test/` directory) for
  any changed code.

- You MUST run `make test` to verify your changes do not break existing
  functionality.

- When changing the project structure, update the Directory Structure section in
  this file so it remains valid.

- If a task is essentially a refactor or improvement of existing code, check
  whether it can be phrased as a code guideline. If so, add it to the relevant
  Code Style section in this file.

- After completing the task you MUST verify that the code you wrote follows the
  Code Style rules in this file.
