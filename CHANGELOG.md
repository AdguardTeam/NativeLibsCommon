# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Added

- Added Keep a Changelog support
- Added tag-based versioning
- Added automated release PR creation workflow

### Changed

### Removed

## [8.1.27] - 2026-05-06

### Fixed

- Fix conan upload on Windows

## [8.1.24] - 2026-05-06

### Added

- Add patch for libevent that disables CHECK_FUNCTION_EXISTS_EX(pipe2) on Apple

## [8.0.29] - 2026-04-13

### Changed

- AG-51611 default interface from netlink route socket

## [8.0.27] - 2026-03-19

### Fixed

- Fix coroutine exception handling to preserve original exception info

## [8.0.23] - 2026-02-25

### Added

- Add `win_get_preferred_adapter_guid` to `net_utils.h`

## [8.0.22] - 2026-02-25

### Added

- Add ngtcp2 patch to disable building examples

## [8.0.21] - 2026-02-25

### Removed

- Remove unused dependency from quiche

## [8.0.19] - 2026-02-20

### Fixed

- Fix win_get_if_nameserver returning an incorrect string

## [8.0.18] - 2026-02-18

### Changed

- Make WFP entities names customizable and add a function to retrieve NameServer property

## [8.0.17] - 2026-02-16

### Changed

- Move some Windows-specific utilities from vpn-libs into the `common` module

## [8.0.16] - 2026-02-13

### Added

- Add missing memory header to network_monitor.h

## [8.0.15] - 2026-02-04

### Changed

- libuv: Add eventfd fallback for old Linux kernels (2.6.22+)

## [8.0.14] - 2026-01-26

### Changed

- Improve condition in AnyOfCondAwaitable

## [8.0.13] - 2026-01-26

### Fixed

- !fix NLC: use localtime_from_system_time instead of std::localtime

## [8.0.12] - 2026-01-16

### Changed

- feat NLC: use newer version of core-libs image

## [8.0.11] - 2025-12-25

### Fixed

- Fix klib warning

## [8.0.10] - 2025-12-11

### Changed

- feat NLC: detect rust and NDK versions in quiche recipe

## [8.0.9] - 2025-12-05

### Added

- Add word wrap

## [8.0.8] - 2025-12-02

### Fixed

- Fix MoveOnlyFunction + small script fix

## [8.0.7] - 2025-11-25

### Changed

- ci: change macos image to tahoe

## [8.0.6] - 2025-11-19

### Changed

- ci: update image to use xcode 26.1.1

## [8.0.5] - 2025-11-01

### Changed

- refactor NLC: move copied structs from 'utils' namespace

## [8.0.4] - 2025-10-22

### Changed

- refactor native-libs-common: copy retrieve_system_dns_servers from vpn-libs

## [8.0.3] - 2025-10-21

### Changed

- Extend AutoPod with possibility to obtain data after release

## [8.0.2] - 2025-10-20

### Fixed

- fix native-libs-common: use strerror, compile NetworkMonitor on Windows

## [8.0.1] - 2025-10-20

### Fixed

- Fix string_view out of boundaries access in validate_gmt_tz

## [7.0.45] - 2025-10-16

### Changed

- Extend AutoFd functional

## [7.0.44] - 2025-10-16

### Fixed

- fix native-libs-common: add necessary framework in conanfile.py

## [7.0.43] - 2025-10-16

### Changed

- refactor native-libs-common: copy NetworkMonitor from vpn-libs

## [7.0.42] - 2025-10-08

### Changed

- Split Conan profiles into musl and non-musl

## [7.0.41] - 2025-10-01

### Fixed

- Maybe fix use-after-free due to a data race

## [7.0.40] - 2025-09-29

### Changed

- LruTimeoutCache: fix timeout not resetting on reinsertion and add a new feature

## [7.0.39] - 2025-09-19

### Added

- Add ag::CidrRangeSet for quick CIDR range inclusion tests

## [7.0.38] - 2025-09-19

### Changed

- Use ephemeral Windows agent

## [7.0.37] - 2025-09-05

### Added

- Add MoveOnlyFunction

## [7.0.36] - 2025-09-01

### Changed

- Move custom_client_random from s3 to ssl_st

## [7.0.35] - 2025-08-29

### Added

- Add AutoFd to commons

## [7.0.34] - 2025-08-29

### Changed

- Use new conan user and channel

## [7.0.33] - 2025-08-28

### Changed

- Replaced user and channel in conan recipes

## [7.0.32] - 2025-08-20

### Changed

- Decreased the size of SocketAddress

## [7.0.31] - 2025-08-20

### Fixed

- [FIX] Do not change dir before calling upload script

## [7.0.30] - 2025-08-20

### Changed

- Patch Boringssl to add an ability to use custom TLS client random

## [7.0.29] - 2025-08-15

### Changed

- Do not report client's handshake completion early, wait for server confirmation

## [7.0.28] - 2025-08-11

### Removed

- Remove stream only in case of Http1Server

## [7.0.27] - 2025-08-11

### Added

- Add escape_argument_for_shell function

## [7.0.26] - 2025-07-30

### Changed

- Patch nghttp2 to allow repeated Content-Length headers

## [7.0.25] - 2025-05-16

### Added

- Added lite log without log level and thread id

## [7.0.24] - 2025-05-14

### Changed

- Use new images

## [7.0.23] - 2025-05-07

### Fixed

- Fix cmake_minimum_required for recipes pcre2

## [7.0.22] - 2025-05-06

### Fixed

- Fix cmake_minimum_required for recipes boring-2024-09-13

## [7.0.21] - 2025-05-06

### Added

- Added patch for miniz

## [7.0.20] - 2025-04-22

### Added

- Add missing <optional> include to logger.h

## [7.0.19] - 2025-04-17

### Added

- Add a wrapper around popen to common/utils.h

## [7.0.18] - 2025-04-11

### Added

- Add char_traits specialization for unsigned char on newer libc++ versions

## [7.0.17] - 2025-03-17

### Fixed

- Fix RotatingLogFile for multi-threading

## [7.0.16] - 2025-02-27

### Added

- Add the ability to patch openssl-quic

## [7.0.15] - 2025-02-14

### Added

- Add sanitizer for Apple

## [7.0.14] - 2025-02-13

### Fixed

- Fix calculation of Base64 encoding size

## [7.0.13] - 2025-01-30

### Fixed

- Fix fmt syntax check for logger with newer clang-tidy

## [7.0.12] - 2025-01-29

### Changed

- Firefox pulp curves support for SSL_set1_group_ids

## [7.0.11] - 2025-01-24

### Added

- Add encode_to_base64 with OutputIterator version

## [7.0.10] - 2025-01-16

### Changed

- Update some BoringSSL patches

## [7.0.9] - 2024-12-24

### Changed

- Correct cpp_info.libs for ngtcp2-1.0.1

## [7.0.2] - 2024-12-10

### Changed

- Use new Mac agent

## [7.0.1] - 2024-11-07

### Added

- Add sanitizer handling

## [6.1.22] - 2024-11-05

### Changed

- Extend commons for CoreLibs

## [6.1.21] - 2024-10-28

### Fixed

- Fixed write if file handler fail

## [6.1.20] - 2024-10-24

### Fixed

- Fixed log rotation

## [6.1.19] - 2024-10-23

### Added

- Added RotatingLogToFile class

## [6.1.18] - 2024-10-18

### Added

- Add LICENSE.md

## [6.1.17] - 2024-10-01

### Changed

- Update BoringSSL to 2024-09-13

## [6.1.16] - 2024-09-10

### Changed

- Duplicated functions moved

## [6.1.15] - 2024-09-05

### Added

- Add a custom Conan setting and publish Conan-related files to Github

## [6.1.14] - 2024-09-03

### Changed

- Disable frame pointer omission when compiling Conan dependencies

## [6.1.13] - 2024-08-27

### Fixed

- Fix missing <functional> header

## [6.1.12] - 2024-08-27

### Changed

- Allow to inspect LRU cache

## [6.1.11] - 2024-08-05

### Changed

- BoringSSL firefox imitation

## [6.1.10] - 2024-06-18

### Fixed

- Fix libevent's function existence checking for MSVC

## [6.1.9] - 2024-06-11

### Changed

- Build BoringSSL on ARM64 Windows

## [6.1.8] - 2024-06-06

### Changed

- AG-33325 Add log level override

## [6.1.7] - 2024-06-06

### Changed

- Use fmt's time formatter for logging

## [6.1.6] - 2024-05-28

### Changed

- Mark a const function as const

## [6.1.5] - 2024-05-24

### Added

- Add `pretty_str` to Error

## [6.1.3] - 2024-05-22

### Fixed

- Fix strict format string

## [6.1.2] - 2024-05-21

### Changed

- Update docker image to core-libs:2.1

## [6.1.1] - 2024-05-21

### Added

- Add StrictFormatString which fails on extra arguments, use it in logger, AG_FMT

## [6.0.17] - 2024-05-02

### Added

- Add is_any() method to SocketAddress

## [6.0.16] - 2024-05-02

### Changed

- Autodetect conan profile

## [6.0.14] - 2024-04-15

### Changed

- AG-31925 Fix libsodium for Intel Celeron processors

## [6.0.13] - 2024-03-21

### Removed

- Remove symbols conflicts from ldns windows build

## [6.0.5] - 2024-03-18

### Added

- Add more patches for `nghttp2`, `ngtcp2`, `nghttp3`, `libevent` and `openssl` recipes

## [6.0.2] - 2024-03-13

### Fixed

- Fix split_by for big-endian arches

## [5.0.14] - 2024-03-11

### Changed

- Unpin from BoringSSL

## [5.0.13] - 2024-03-06

### Changed

- Update boringssl and quiche recipes

## [5.0.12] - 2024-02-14

### Changed

- Make fmt format a SocketAddress as a string

## [5.0.11] - 2024-02-13

### Removed

- Remove Conan 1.x files, rename conan2 directory

## [5.0.2] - 2024-01-20

### Removed

- Remove misleading return type

## [5.0.1] - 2024-01-19

### Changed

- Replace perfect forwarding of awaitables to guaranteed copy/move (it also does not break RVO!)

## [4.0.24] - 2024-01-19

### Changed

- Enable perfect forwarding of awaitables in ag::parallel

## [4.0.23] - 2024-01-16

### Changed

- Return host_str without brackets in SocketAddress

## [4.0.22] - 2024-01-15

### Changed

- AG-29427: as_u8v modified

## [4.0.21] - 2024-01-14

### Changed

- AG-29427: Used ag::as_u8v

## [4.0.20] - 2024-01-12

### Added

- AG-29427: Added Uint8Span alias, conversion functions, and unit tests

## [4.0.11] - 2023-12-05

### Fixed

- Fix a racy HTTP/3 test

## [4.0.9] - 2023-12-01

### Removed

- Remove `boringssl-vendored` feature from quiche recipe

## [4.0.8] - 2023-12-01

### Fixed

- Fix build on MSVC 19.38

## [4.0.7] - 2023-12-01

### Changed

- Improve quiche recipe & use newer boring

## [4.0.6] - 2023-11-29

### Fixed

- Fix building with GCC

## [4.0.5] - 2023-11-29

### Removed

- Remove bad version of boringssl

## [4.0.1] - 2023-11-28

### Fixed

- Fix some bugs in Conan 2 recipes

## [3.0.27] - 2023-11-28

### Changed

- Migrate to Conan 2.0

## [3.0.25] - 2023-11-23

### Added

- Add http3 error codes and on_available_streams callback for HTTP/3

## [3.0.15] - 2023-11-21

### Changed

- Downgrade boringssl to last stable Win7 compatible version

## [3.0.8] - 2023-11-15

### Changed

- Migrate to conan 2.0 syntax

## [3.0.6] - 2023-11-08

### Fixed

- Fix Win64 build

## [3.0.5] - 2023-10-31

### Added

- Add some URL and string utils

## [3.0.4] - 2023-10-11

### Changed

- Use the specific version of core-libs image

## [3.0.3] - 2023-10-11

### Added

- Introduce a custom conan recipe for llhttp to avoid linking against the dynamic library

## [3.0.2] - 2023-10-09

### Fixed

- Fix leaks in HTTP/3 tests

## [3.0.1] - 2023-10-03

### Changed

- Extend logger workaround scope to every clang platform

## [2.0.72] - 2023-09-29

### Changed

- HTTP/1/2/3

## [2.0.71] - 2023-09-19

### Fixed

- Fix building boringssl with Xcode 15

## [2.0.70] - 2023-09-09

### Changed

- Use explicit ctor for Awaitable to prevent MSVC UB

## [2.0.69] - 2023-08-25

### Changed

- Minor error refactoring

## [2.0.68] - 2023-06-29

### Added

- Add `split2_by_any_of` and tests for `split2_*` functions

## [2.0.67] - 2023-06-19

### Changed

- Use view instead of vector in `CidrRange::contains`

## [2.0.66] - 2023-06-17

### Changed

- Expand CidrRange::contains to handle various input formats

## [2.0.60] - 2023-05-23

### Changed

- SocketAddress and some other improvements

## [2.0.57] - 2023-05-05

### Changed

- Upgrade quiche to 0.17.1

## [2.0.56] - 2023-04-24

### Changed

- Make SocketAddress a bit more convenient

## [2.0.55] - 2023-03-16

### Fixed

- Fix quiche recipe for linux

## [2.0.54] - 2023-03-13

### Added

- add SocketAddress constructor from IpAddress

## [2.0.53] - 2023-02-28

### Changed

- Roll curl back to 7.85 and add more patches to that

## [2.0.52] - 2023-02-22

### Changed

- Update CURL and ngtcp2

## [2.0.51] - 2023-02-20

### Changed

- Disable optimization when making fmt format args

## [2.0.49] - 2023-02-16

### Fixed

- Fix/header only fmt test

## [2.0.48] - 2023-02-16

### Changed

- Header only fmt test

## [2.0.47] - 2023-02-14

### Changed

- Some logger optimizations

## [2.0.46] - 2023-02-09

### Changed

- Upgrade quiche to 0.16.0

## [2.0.45] - 2023-01-20

### Fixed

- Fix readme

## [2.0.44] - 2023-01-17

### Changed

- Allow exporting a single version to local conan cache

## [2.0.43] - 2022-12-27

### Changed

- Export system libs on Windows

## [2.0.42] - 2022-12-26

### Changed

- Improve performance of IPv6 address parsing on Windows

## [2.0.41] - 2022-12-26

### Added

- Add a TLD registry conan package

## [2.0.40] - 2022-12-06

### Changed

- Patch BoringSSL's disabled MSVC warnings list

## [2.0.39] - 2022-12-01

### Added

- Add missing tools import in conanfile

## [2.0.38] - 2022-11-29

### Changed

- Restore ngtcp2 0.8.0

## [2.0.37] - 2022-11-28

### Changed

- nghttp3 crash on old cpu when calling nghttp3_ringbuf_init

## [2.0.36] - 2022-11-28

### Changed

- Ngtcp2 crash on old cpu when calling ngtcp2_ringbuf_buf_init

## [2.0.35] - 2022-11-25

### Added

- Introduce a cross-platform constant for ECONNRESET

## [2.0.34] - 2022-11-17

### Fixed

- Fix make_error macro

## [2.0.33] - 2022-11-10

### Changed

- Allow to truncate the file opened for writing

## [2.0.32] - 2022-10-31

### Changed

- Enable container printing

## [2.0.30] - 2022-10-14

### Fixed

- Fix socket address constructor on Windows

## [2.0.29] - 2022-10-05

### Added

- Add extra allowed magic numbers

## [2.0.28] - 2022-10-03

### Fixed

- Fix bamboo spec

## [2.0.27] - 2022-09-29

### Added

- Add a patch for cURL not reusing QUIC connections

## [2.0.26] - 2022-09-29

### Changed

- Build cURL with HTTP/3 support

## [2.0.25] - 2022-09-23

### Fixed

- Fix parallel::any_of_cond not supporting move-only return types

## [2.0.24] - 2022-09-23

### Removed

- remove ErrString and replace usage with Error/Result

## [2.0.22] - 2022-09-13

### Changed

- Backport some EPROTOTYPE fixes from master

## [2.0.21] - 2022-09-13

### Added

- Add test to ensure that all of parallel::all_of tasks are executed

## [2.0.18] - 2022-08-30

### Added

- Add chrono formatter by default

## [2.0.17] - 2022-08-26

### Added

- Introduce append flag for file

## [2.0.16] - 2022-08-24

### Fixed

- Fix minor issues

## [2.0.15] - 2022-08-23

### Changed

- Make SocketAddress accept IPv6 addresses with a scope ID

## [2.0.14] - 2022-08-22

### Changed

- Conan recipe: ngtcp2 0.8.0

## [2.0.13] - 2022-08-19

### Added

- Add a method to change the port of a socket address

## [2.0.12] - 2022-08-09

### Added

- Add coroutines support to NLC

## [2.0.11] - 2022-08-05

### Changed

- Export FMT_EXCEPTIONS=0 define

## [2.0.3] - 2022-07-19

### Changed

- Use global namespace in log macros

## [2.0.2] - 2022-07-19

### Changed

- Port LruTimeoutCache from vpn-client

## [1.0.14] - 2022-05-25

### Changed

- Make codestyle even greater

## [1.0.13] - 2022-05-24

### Added

- Add common code style

## [1.0.12] - 2022-05-24

### Added

- Add nanos

## [1.0.10] - 2022-04-11

### Added

- Fixed join() function & added more tests

## [1.0.9] - 2022-04-08

### Added

- Added CidrRange class & some fixes

## [1.0.8] - 2022-04-07

### Changed

- Minor cmake toolchain fix

## [1.0.3] - 2022-03-16

### Fixed

- Fix std::move not actually moving

## [1.0.0] - 2022-02-07

### Added

- Introduce Error class

