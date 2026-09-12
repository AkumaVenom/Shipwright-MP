> Historical validation from the earlier Direct-IP source preview. Raw environment logs are not redistributed with the GitHub snapshot. This record is not a claim that the consolidated game builds. See [repository validation](../publishing/VALIDATION.md) for checks performed on this package.

# Validation record

Date: 12 September 2026. Source edition: Direct-IP 1.0.0 implementation preview.

## Executed

The standalone native test executable builds the actual production `Transport.cpp` and includes the actual production `ProgressRules.h`. It does not substitute mock sockets. All live socket tests ran over loopback on Linux; the eight-player capacity test connects seven network clients to a host transport and rejects an additional client.

- GCC 14.2.0: **21/21 native cases passed**, with `-Wall -Wextra -Wpedantic -Werror`, AddressSanitizer, UndefinedBehaviorSanitizer and leak detection.
- Clang: **21/21 native cases passed** with the same warnings/sanitizers. See the configure log for its detected version.
- CTest: one registered executable containing those 21 cases passed for each compiler. This is not 21 separate CTest test registrations.
- **19/19 static source-integration assertions passed.** They check the presence/order of integration safeguards; they do not compile or execute the game adapter.

The native cases cover IPv4 validation (including padded all-zero rejection), byte-by-byte and coalesced frame decoding, zero/oversized frames, reset, 600 randomized fragmented binary payloads, packed upgrade tiers, heart totals, inventory validation, personal item slots, bidirectional TCP, three-client addressing, a 512 KiB binary payload, a 500-message ordered burst, rejection flush before close, reconnect/host loss, occupied/refused ports, eight-participant capacity, cancellation/reuse, malformed live wire, live fragmented wire, and bounded overload handling/shutdown against a stalled receiver.

The first test iteration passed 18/19 cases. The stalled-receiver test initially assumed overload must return `false` directly from `Send`; the production transport is also designed to disconnect an overflowing peer. The test was corrected to accept either observable bounded-overload outcome, not to remove the queue assertion. Two additional merge/validation cases were then added; the resulting 21-case suite passed under both compilers. No sanitizer findings occurred in the final native runs.

## Full game build attempt

Initial CMake configuration could not find `lsb_release`. A temporary, test-environment-only wrapper read the actual distribution from `/etc/os-release` so configuration could continue. It then attempted the baseline's spdlog FetchContent clone and failed with `Could not resolve host: github.com`. See `logs/full-build-configure.log`.

Thus **the full game's new C++ adapter/menu and modified engine files have not been compilation-verified**. A successful native transport build is not evidence that the entire application builds. No Windows/MSVC or macOS build, rendered gameplay, ROM-based runtime test, end-to-end save restoration, real LAN/two-PC test or long-duration gameplay soak was executed. Missing dependency/network access and absent runtime game data remain the verification barriers.

## Reproduce native tests

From the modified source root, using an installed C++20 compiler and CMake:

```sh
cmake -S tests/direct_multiplayer -B build/direct-tests -DCMAKE_BUILD_TYPE=Debug -DDIRECT_SANITIZERS=ON
cmake --build build/direct-tests --parallel
ctest --test-dir build/direct-tests --output-on-failure
python3 tests/direct_multiplayer/source_contract_tests.py
```

For MSVC, omit `-DDIRECT_SANITIZERS=ON`, build with `--config Debug`, and pass `-C Debug` to CTest. The Windows test target links `ws2_32`. Native tests do not need the game's dependency tree or copyrighted assets. The source-contract script requires Python 3.

Full-game build and acceptance instructions are in [DIRECT_MULTIPLAYER.md](DIRECT_MULTIPLAYER.md), [ACCEPTANCE_CHECKLIST.md](ACCEPTANCE_CHECKLIST.md) and the original [BUILDING.md](../BUILDING.md).
