# Native libs common stuff

#### Prerequisites

* Conan C++ package manager 2.0.4 or higher
* CMake 3.24 or higher
* GCC 9 or higher / Clang 8 or higher

## Build

### Linux

```shell
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo -GNinja \
    -DCMAKE_C_COMPILER=clang \
    -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_CXX_FLAGS="-stdlib=libc++"  \
    ..
```

### Windows

```shell
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo ^
    -DCMAKE_C_FLAGS_DEBUG=/MT ^
    -DCMAKE_CXX_FLAGS_DEBUG=/MT ^
    -G Ninja ^
    ..
```

### macOS

```shell
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo -GNinja \
    -DCMAKE_C_COMPILER=clang \
    -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_CXX_FLAGS="-stdlib=libc++" \
    ..
```

Currently, it contains Conan recipes for AdGuard libs

## How to use conan

Conan is a C++ package manager. It is similar to maven, but stores recipes and binaries separately.
Binaries can be uploaded to a repo and reused.

It is recommended to use the custom remote repo as the main repo (`-i 0`).
Conan looks up binaries in the repo from which recipes were downloaded.
So if a recipe is from conan-center, you won't be able to store binaries because it has
the highest priority by default.

```shell
conan remote add --index 0 $REMOTE_NAME https://$ARTIFACTORY_HOST/artifactory/api/conan/$REPO_NAME
```

We customized some packages, so they need to be exported to local conan repository.

```shell
./scripts/export_conan.py
```

If you want to upload built binaries and recipes to conan remote repository, use following command:

```shell
conan upload -r $REMOTE_NAME -c '*'
```

If you want to upload exported only recipes to conan remote repository, use following command:

```shell
conan upload -r $REMOTE_NAME -c '*' --only-recipe
```


## Testing changes as a dependency

To test local changes in the library when it is used as a Conan package dependency,
do the following:

1) If the default `vcs_url` in `<root>/conanfile.py` is not suitable, change it accordingly.
2) Commit the changes you wish to test.
3) Execute `./script/export_conan.py local`. This script will export the package, assigning the last commit hash as its version.
4) In the project that depends on `native_libs_common`, update the version to `<commit_hash>` (where `<commit_hash>` is the hash of the target commit):
Replace `native_libs_common/1.0.0@adguard_team/native_libs_common` with `native_libs_common/<commit_hash>@adguard_team/native_libs_common`.
5) Re-run the cmake command.
   Note:
    * If you have already exported the library in this way, the cached version must be purged: `conan remove -f native_libs_common/<commit_hash>`.

## Code style

### Common

1. Indentation is 4 spaces (imported files may have another indent).
2. Code must be commented enough in terms of control flow. All public and big static methods should have a description in Doxygen format (`/** */`)
3. Formatting rules. We use CLion basic rules. They are based on LLVM and Apple rules, and applies 4 spaces indentation by default. 
   But there are also rules not related to indentation:
    - Binary operator are always separated by spaces from their operands (x + y)
    - Screen width: 120 symbols (not 80)
    - In function definition, operator block is started on function line `int main(int argc, char **argv) {`.
      However, `int func() try {` is prohibited.
      If it makes harder to read the code, next line may be empty:
      ```
          int very_long_function_definition(
                  int foo1234567890, int bar9876543210, int baz9999999999, 
                  int x4242424242, int y1010101010, int z0101010101) noexcept {

              do_smth();
          }
      ```     
    - In loops and ifs, operator block is started on function line too. Single-line branches should be in operator braces too.
4. Identifier prefixes other than `p_`, `m_`, `g_` and `_` is prohibited
5. Don't use `p` prefix unless it is really needed. In most cases type of identifier says what is it.
6. All new headers should contain `#pragma once` guard instead of `#define` one.

### C

No new C code please.

### C++
1. Language standard - C++20 (-std=c++20)
2. Class prefixes - use namespaces instead.
3. Namespaces - root namespace is ag::, max depth is 2 (plus may be ::test).
   Namespace names are snake_cases.
4. Class naming - Capital CamelCase (Rust-like)
5. Using naming - Capital CamelCase.
   If it is wrapped pointer to type with `snake_case`, then it may be named `snake_case_ptr`.
6. Instance naming - snake_case
7. Member function, non-member variable and parameter naming - `snake_case`
8. Member variable naming - `m_snake_case`, public members of struct are `snake_case`.
9. Member access - `m_something`, or `this->something` if `m_` is missing in name.
10. Constants and enum values (static const, constexpr and defines) naming - `CAPITAL_UNDER_SCORE`
11. Global variables - `g_snake_case`
12. Includes order - Chromium-style:
    a. List of external headers in `<>`
    b. List of our public headers, alphabetically sorted, in `""`.
       If current unit is implementation of the public header, that include should anyway be included here.
    c. List of current directory headers, alphabetically sorted, in `""`.
       This section should not exist in public headers.

Code example:

```
#pragma once

#include <string>

namespace ag::utils {
    class Strings {
    public:
        /**
         * Make greeting to the world
         * @param x Some random value
         * @return The greeting
         */
        static std::string hello_world(int x);
    }
}

```

```
#include <string>

#include "common/utils.h"

#include "another_utils.h"

namespace ag::utils {
    std::string Strings::hello_world(int x) {
        if (x % 2) {
            return "Hello, world!!";
        } else {
            return "Hello, world!1";
        }
    }
}
```

### Doxygen comments
- All public methods and functions should be documented.
- Use Javadoc style with an `autobrief` feature.
- `autobrief` means that the first statement of a long description automatically becomes a brief description.
  So `@brief` is redundant.
- Use `@return`, not `@returns`
- Use `[out]` in `@param` only if code is not self-explanatory.
- Don't use `[in]` in `@param`.
- Don't use extra line endings.
- Use infinitive instead of the third person in brief descriptions.
- Descriptions should start with a capital letter.

Examples:
```
/**
 * Sum of x and y.
 * This function is usually used to get sum of x and y.
 * @param x The x
 * @param yy The y
 * @return Sum of x and y.
int plus(int x, int yy) {
    return x + yy;
}
enum class t {
    A, /**< One-line post-identifier comment */
    /** Another one-line comment */
    B,
    /** Third one-line comment */
    C,
    D, /**< One-line post-identifier comment */
}
```
