# ClientHello profile integration test

`clienthello_profiles_test.cpp` checks that the ClientHello produced by
`make_ssl` still matches the ClientHello of a real browser. It runs Chrome,
Chrome Canary, Firefox and Safari against a local TLS server, captures the
raw bytes of the first TLS record, and compares the JA4 fingerprint of the
browser with the JA4 fingerprint of the matching `TlsClientProfile`.

The test is not part of `make test`. It needs macOS, installed browsers and
a GUI session, so pull-request CI does not build or run it. The
`clienthello-profiles.yml` workflow runs it every week instead.

## Requirements

- macOS with a GUI session. Safari cannot run without one.
- Google Chrome in `/Applications/Google Chrome.app`.
- Google Chrome Canary in `/Applications/Google Chrome Canary.app`.
- Mozilla Firefox in `/Applications/Firefox.app`.
- Safari in `/Applications/Safari.app`.

A missing browser is skipped by default. Set
`AG_CLIENTHELLO_REQUIRED_BROWSERS` to make a missing browser fail, as the
scheduled workflow does. The scheduled workflow installs Chrome, Chrome
Canary and Firefox with Homebrew before the run. The macOS runner image
ships Safari only.

## Run the test locally

```shell
make PRESET=clang-debug test-clienthello
```

This command configures with `-DNLC_CLIENTHELLO_TESTS=ON`, builds the test
target, and runs the test. The JUnit report is written to
`cmake-build-clang-debug/clienthello-junit.xml`. The artifacts are written
to `cmake-build-clang-debug/clienthello-artifacts`.

To run a single case:

```shell
./cmake-build-clang-debug/tls/clienthello_profiles_test \
    --gtest_filter='ClientHelloProfiles.Chrome'
```

The test starts a new browser window with a temporary profile for Chrome,
Chrome Canary and Firefox. Safari is opened through `open -a Safari` and the
test leaves one tab open.

## What the test checks

For each browser the test does the following:

1. Starts a TLS server on `127.0.0.1:0` with a self-signed certificate and
   the `h2` and `http/1.1` ALPN protocols.
2. Opens `https://localhost:<port>/` in the browser.
3. Reads the first TLS record from the server socket without consuming it.
4. Parses the ClientHello with `tls/test/client_hello.cpp` and computes
   JA4 with `tls/test/ja4.cpp`.
5. Builds the reference ClientHello with `make_ssl` for the matching
   profile, using the browser's SNI and a canonical `h2`, `http/1.1` ALPN
   list. The list is deliberately not the browser's list, so an ALPN
   difference shows up in the diff.
6. Compares the two JA4 fingerprints.
7. Asserts the supported groups, key share groups and supported versions
   separately, because JA4 ignores the group lists and only takes the
   highest supported version into account.

The test fails when JA4 differs, or when the supported groups, key share
groups or supported versions differ, including their wire order. JA4
hashes the cipher suite set, the extension set and the signature
algorithms, and it takes the first and last character of the first ALPN
protocol. A difference in the cipher suite order, the extension order or
in the ALPN list beyond the first protocol is written to the artifacts but
does not fail the test. Chrome permutes the extension order on every
handshake, so for Chrome profiles the test compares the extension set, not
the order.

## Environment variables

| Variable | Default | Purpose |
| --- | --- | --- |
| `AG_CLIENTHELLO_ARTIFACTS_DIR` | unset (the `make` target sets `cmake-build-<preset>/clienthello-artifacts`) | Directory for the JA4 report, raw ClientHello bytes and diffs. |
| `AG_CLIENTHELLO_REQUIRED_BROWSERS` | unset | Comma-separated browser ids that must be installed: `chrome`, `chrome-canary`, `firefox`, `safari`. |
| `AG_CLIENTHELLO_BROWSER_MODE` | `gui` | Set to `headless` to start Chrome, Chrome Canary and Firefox without a window. Safari always uses the GUI. |

Use `headless` only to compare the fingerprints of both modes. The test
uses the GUI mode by default, because the profiles must match the mode that
real users run and Safari has no headless mode.

Checked on Chrome 157.0.8103.40: the `--headless=new` ClientHello has the
same JA4 as the GUI ClientHello. Re-check this with
`AG_CLIENTHELLO_BROWSER_MODE=headless` after a large Chrome update.

## Test certificate

`tls/test/data/clienthello_test_cert.pem` and
`tls/test/data/clienthello_test_key.pem` are a self-signed certificate for
`localhost` with a 100-year validity. They are test data, not a secret.

## Artifacts

When `AG_CLIENTHELLO_ARTIFACTS_DIR` is set, the test writes:

- `ja4-profiles.txt` - JA4 of the four browser profiles (`CHROME`,
  `CHROME_CANARY`, `FIREFOX`, `SAFARI`). The other profiles are covered by
  `MakeSsl.Ja4Profiles` in `tls/test/make_ssl_test.cpp`; `BORINGSSL_DEFAULT`
  is printed there but deliberately not asserted.
- `ja4-browsers.txt` - browser id, browser version and browser JA4.
- `clienthello-<browser>.bin` - raw first TLS record.
- `clienthello-<browser>.txt` - parsed ClientHello and JA4.
- `diff-<browser>.txt` - parameter diff, written when the raw ClientHello
  differs from the profile.

The scheduled workflow uploads this directory with the JUnit report.

## Update the profiles after a confirmed browser change

When the test fails, first check that the browser change is real and not a
local experiment. Then do the following:

1. Read `diff-<browser>.txt` and the raw ClientHello in the artifacts.
2. Update `tls/make_ssl.cpp`:
   - the cipher list, groups and signature algorithms in
     `cipher_list_for()`, `groups_for()` and `sigalgs_for()`;
   - the min/max protocol versions (`SSL_set_min_proto_version()` and
     `SSL_set_max_proto_version()`);
   - the extension order in `extension_order_for()`;
   - the profile-specific blocks in `make_ssl()`.
   The key-share groups follow the `groups_for()` order.
3. Run `make PRESET=clang-debug test-clienthello` again until the JA4
   fingerprints and the supported groups, key share groups and supported
   versions match.
4. Update the expected values in `tls/test/make_ssl_test.cpp`:
   - the JA4 fingerprints in `MakeSsl.Ja4Profiles`;
   - the groups, key-share groups and supported versions in
     `MakeSsl.ParsedClientHelloFields`.
   The test prints the JA4 values, and they are also in
   `ja4-browsers.txt`.
5. Run `make test` to check the unit tests.

A new extension or signature algorithm can need a BoringSSL update. In that
case, bump the OpenSSL/BoringSSL Conan package first, then update the
profile.

## Why the test is not in `make test`

The test starts GUI applications and needs a browser installation. It is
slow and can fail because of the machine state, not because of the code.
Keep it out of `make test` and out of `run-tests.yml` so that normal checks
stay fast and stable.
