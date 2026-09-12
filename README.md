# Shipwright-MP — Direct-IP multiplayer source preview

**Unofficial, experimental source snapshot. Not a compiled game or a validated multiplayer release.**

This repository consolidates the supplied Shipwright baseline, the Direct-IP multiplayer source changes, Windows helper 1.2, and both reported compile corrections. No earlier patch ZIP needs to be applied to this snapshot. Repository packaging does not change the multiplayer scope.

**Publishing source does not require compiling the game.** Start with [PUBLISH_TO_GITHUB.md](PUBLISH_TO_GITHUB.md). To build locally later, use [BUILD_WINDOWS.md](BUILD_WINDOWS.md).

## Implemented source scope

The in-game **Network → Multiplayer** page offers **Offline / Single Player**, **Host & Play**, and **Join by IP**. Hosting is embedded in the game; direct TCP uses port **43384** by default, with up to eight participants including the host. The source connects the supplied Anchor player/progression replication to this transport.

Each player loads a local save slot. Personal-save mode is the default. Shared-adventure progression is optional and requires agreement from the host and joiners. The code backs up the selected on-disk save before joining/hosting. Save normally before connecting for an up-to-date backup. Shared progress can persist through normal saves/autosaves and is not rolled back on disconnect. Read the [save and replication policy](docs/multiplayer/DIRECT_MULTIPLAYER.md) before testing.

**Enemy AI, shared enemy/boss health, projectiles, physics and cutscene timing are not host-authoritative or fully replicated.** The complete modified game has not been successfully compiled or gameplay-tested in the packaging environment. Do not describe this as full co-op, fully synchronized multiplayer, or a stable release. Use trusted private sessions only: transport has no password authentication or encryption.

## Source layout

`soh/` contains game source and project asset definitions. `libultraship/` and `torch/` are the exact bundled dependency trees from the supplied archive, tracked as ordinary folders, **not Git submodules**. Their former `.git` pointer files referenced missing history and have been removed. Do not run submodule update commands for this snapshot. Other CMake dependencies still require network access when building.

The five binary font assets are not bundled. A game configure retrieves them from upstream and verifies the SHA-256 fingerprints of the original supplied assets before compilation proceeds. Existing matching files are reused. A documented local-source alternative is available when offline. Download availability has not been verified from this environment. See [BUILD_WINDOWS.md](BUILD_WINDOWS.md).

## Validation and reporting

[Repository validation](docs/publishing/VALIDATION.md) distinguishes source-import checks and standalone tests from full game compilation. [Two-PC acceptance checklist](docs/multiplayer/ACCEPTANCE_CHECKLIST.md) lists the outstanding gameplay checks. Report problems against **this unofficial repository**, not as confirmed defects in upstream Shipwright. Do not upload saves, ROMs, extracted game archives or raw logs containing personal machine paths.

The original root GitHub Actions workflows are preserved under [docs/publishing/upstream-workflows](docs/publishing/upstream-workflows), outside the active workflow directory. Importing this source does not automatically start unverified builds or publish releases. No full-game CI success is claimed.

## Attribution and existing notices

Based on Ship of Harkinian / Shipwright by HarbourMasters, the Ocarina of Time decompilation contributors, libultraship, Torch, and the third-party projects included in the baseline. This is not an official HarbourMasters release. Original [credits](docs/CREDITS.md), [upstream README](README_UPSTREAM.md), dependency licences and per-file notices are retained.

The supplied ZIP did not contain top-level Git commit history or a root licence file. No upstream commit identity or blanket replacement licence is invented here. See [PROVENANCE.md](PROVENANCE.md). Preserve the existing notices when distributing or modifying this source. Players must supply their own supported game data; no ROM or ROM-extracted archive belongs in this repository.
