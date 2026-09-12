# Source provenance and packaging policy

## Inputs

This is a new source snapshot assembled from the exact archives supplied in this conversation, not a fresh download of upstream and not a reconstructed upstream Git history. The machine-readable [input and change manifest](docs/publishing/SOURCE_MANIFEST.json) records hashes and changes.

The input is `Shipwright-MP_1.0.0_DirectIP_SourcePreview.zip`, retaining the original supplied game and dependency files, overlaid with the two game-source files from `Shipwright-MP_Compile_Fix_1.2.zip`. Helper launchers come from the Windows helper ZIP; the latest helper and resume launcher come from compile fix 1.2. Its installer/build behaviour is retained; one diagnostic no longer directs readers to the superseded prepare-source ZIP.

## Dependency identity

The original `.gitmodules` recorded:

- `libultraship`: https://github.com/kenix3/libultraship.git, branch `port-maintenance`.
- `torch`: https://github.com/HarbourMasters/Torch.git, branch `main`.

The ZIP contained populated dependency source folders, but their `.git` files pointed to absent `.git/modules/...` metadata. Those pointers and the root `.gitmodules` are removed **in this publishing copy only**. Both source trees are ordinary tracked folders. Their exact upstream commit SHAs and the game commit history are not supplied, so no commit relationship is asserted. Do not substitute current upstream dependencies for the bundled source.

## Notices and attribution

Original credits, dependency licences and per-file copyright/attribution notices remain. No root `LICENSE` file was present in the supplied snapshot; this package does not invent one or purport to relicense all inherited code. A complete licence/provenance audit has not been performed. Font binaries are excluded and acquired separately by content-checked retrieval.

The six PEM files under `libultraship/keys/script` begin with `BEGIN PUBLIC KEY` and are bundled upstream verification keys, not private signing credentials. They are retained. StormLib also contains an embedded legacy MPQ signing key in `torch/lib/StormLib/src/SFileVerify.cpp`. This source file is retained byte-for-byte from the supplied baseline; the credential-pattern scan flags it explicitly rather than pretending there are no private-key markers anywhere. No user build log, ROM, save, access token or local Git metadata is intentionally included. File/content scans are checks, not a guarantee about every historical upstream asset's rights.

## Packaging changes

Required source helpers, icons, bundled project templates and libtomcrypt headers are explicitly tracked instead of being hidden by rules intended for an already-tracked upstream repository. Build caches, outputs, ROMs, extracted archives, saves, logs and private configuration are excluded. Original root workflow YAML files are preserved as inactive references under `docs/publishing/upstream-workflows`; there are no active Actions workflows in this snapshot.

The former root README remains as `README_UPSTREAM.md`; the new root README describes this unofficial preview, not upstream release downloads. Fonts are restored early at game configuration using the manifest's source-derived SHA-256 values. No additional gameplay implementation is made in this packaging revision.
