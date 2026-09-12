# Consolidated snapshot entry points

For GitHub publication, read [PUBLISH_TO_GITHUB.md](PUBLISH_TO_GITHUB.md). **No compilation is required.** For an optional new local Windows build, read [BUILD_WINDOWS.md](BUILD_WINDOWS.md). The compile fixes and corrected helper are already included; do not apply the older ZIPs here.

# Shipwright direct-IP multiplayer — source preview

**This ZIP contains source, not a ready-to-run `soh.exe`.** The native network transport is tested, but the full game build/menu, saves and two-PC gameplay still need validation. It adds direct hosting to the supplied Anchor-based game; it does **not** implement synchronized enemy AI/boss health, physics, projectiles or cutscenes.

## Build this source

Keep your original accepted build and save files. Extract this archive to a separate development directory. Use the existing [build instructions](docs/BUILDING.md) with **this modified source tree**, not a fresh unmodified upstream download. The uploaded libultraship and torch trees are retained. Missing third-party build dependencies may need Internet access to fetch. You still supply your own supported game data.

For the Visual Studio 2022 toolchain described by the baseline, the normal PowerShell commands from the source root are:

```powershell
cmake -S . -B build/direct-ip -G "Visual Studio 17 2022" -T v143 -A x64 -DCMAKE_BUILD_TYPE=Release
cmake --build build/direct-ip --config Release --target GenerateSohOtr
cmake --build build/direct-ip --config Release
```

The executable and asset locations depend on that CMake generator/configuration. See the build output and original build documentation; this package does not contain or claim a successfully compiled Windows executable.

## In game

Open **Esc → Network → Multiplayer**.

**Host:** choose **Host & Play**, enter a player name, leave TCP port **43384** or choose another available port, then press **Launch selected mode**. Load your own normal save if you are at file select. Wait for **Hosting**.

**Joiner:** choose **Join by IP**, enter the host PC's numeric LAN IPv4 address, use the same port and press **Launch selected mode**. Load your own save normally. `127.0.0.1` is only for same-computer tests with separate save directories.

Allow inbound TCP for the host application/selected port. Internet play needs reachable addressing/forwarding or a trusted VPN; there is no relay or automatic NAT setup. Use only trusted private sessions; this protocol is not encrypted or password-authenticated.

## Choose how saves behave

**Personal-save mode is the default.** You see the other players without copying their inventory or world progression. To share the adventure, enable sharing on the host and explicitly permit shared progress on each joiner. Initial snapshots merge permanent progress without replacing local slot/identity or the entire inventory. Live shared item events can still award items/resources and affect quests.

A dated copy of each selected on-disk save is made before connecting, and its actual path is shown in the menu. Save normally before launch for a restorable checkpoint of all current progress. **Save & Leave Session** saves locally and disconnects. Disconnect alone does not roll progress back. Save states are blocked during a session; ordinary saves remain supported.

Before accepting this as a new stable baseline, complete the [two-PC acceptance checklist](docs/multiplayer/ACCEPTANCE_CHECKLIST.md). Read the [full save/replication policy](docs/multiplayer/DIRECT_MULTIPLAYER.md) and [actual validation record](docs/multiplayer/VALIDATION.md).
