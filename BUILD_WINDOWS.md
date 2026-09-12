# Optional Windows build — separate from publishing

**You do not need to run any of these steps to put the source on GitHub.** Keep the earlier local build and its cache where they are. This publication snapshot is separate and does not reuse another folder's cache.

This source already contains compile fix 1.2 and the corrected helper. Do not run the older source-preparation or patch ZIPs against it.

## New build only

Use a short local x64 Windows path. `1_SETUP_TOOLS.cmd` checks prerequisites and asks before installation; existing installed tools are detected. `2_BUILD_GAME.cmd` configures this tree, generates project assets, compiles, runs the supplied standalone tests and packages output only when the prior stages succeed. `3_CHECK_TOOLS.cmd` is a prerequisite check only.

`4_RESUME_BUILD.cmd` is only for an already configured build **in this exact folder**. It checks the cached source path/toolchain and does not clean or move caches. It cannot resume a different folder's build.

All helpers retain the earlier validation boundary: they have not been executed on Windows in this packaging environment. A successful source import or standalone transport test is not a successful full game build.

## Source fonts

Five binary fonts used by upstream are not redistributed in this ZIP. Before expensive dependency builds, CMake obtains missing files from upstream with TLS verification and checks SHA-256 values recorded from the supplied baseline in `CMake/source-fonts.json`. Files are downloaded to temporary names and accepted only after hash verification. Existing mismatched files are not overwritten. Downloaded fonts remain ignored by Git.

The URL is on upstream's `develop` branch; the **content** is pinned by SHA-256. If upstream moves/replaces a file, configure intentionally stops rather than using different bytes. Live upstream downloads could not be tested from this environment.

Offline alternative: copy the five matching files from `soh/assets/custom/fonts` in your retained original source into the same folder here, or pass `-DSOH_SOURCE_FONTS_DIR="<original source>/soh/assets/custom/fonts"` in a manual CMake configure. Hash checks still apply. No recompilation or font retrieval occurs when merely publishing the source.

## Advanced commands

With Visual Studio 2022/v143, CMake and other documented prerequisites installed:

```powershell
cmake -S . -B build/direct-ip -G "Visual Studio 17 2022" -T v143 -A x64 -DCMAKE_BUILD_TYPE=Release
cmake --build build/direct-ip --config Release --target GenerateSohOtr
cmake --build build/direct-ip --config Release
```

The helper includes baseline-specific additional settings for CMake 4 and dependency discovery. The original platform [build reference](docs/BUILDING.md) is retained but its upstream cloning/submodule/CI instructions are not applicable to this vendored snapshot. Use the source already here, not a new upstream checkout.

Players still supply their own supported game data for runtime; never commit ROMs, game-data archives or saves. Read the two-PC acceptance checklist before treating any resulting build as stable.
