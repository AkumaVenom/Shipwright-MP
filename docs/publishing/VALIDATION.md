# Consolidated GitHub source: validation record

Date: 12 September 2026. This is a source-publication check, not a successful Windows game build.

## Completed locally

A disposable Git repository was initialized from the consolidated folder. **Normal `git add --all` tracked every intended source file without force-adding ignored files.** The resulting index contained **zero submodule/gitlink entries**. A real local commit and clone succeeded, and the clone contained the entire intended file set. The temporary Git history and test identity are not distributed. The final archive receives a separate post-packaging extraction and staging check.

The dependency source, helper scripts, original icons, bundled project templates and previously ignored libtomcrypt headers survived the clone. The 19 original static multiplayer integration contracts passed again from that clone. Source files for both compile fixes match the hashes in the earlier 1.2 patch manifest.

The standalone native transport/save-rule executable was rebuilt with GCC, C++20, AddressSanitizer and UndefinedBehaviorSanitizer. Its **21 internal cases passed**, and CTest's one registered test executable passed. No full game build was run by this check.

The added source-font restoration routine was executed independently by CMake. Verified: all five original-content files can be restored from a local source folder; their bytes match; existing matching files are not rewritten; existing mismatched files are not overwritten; a `file://` download fixture is accepted only with the expected hash; download/hash failures leave no installed target; unsafe manifest names are rejected. **Real upstream HTTPS font downloads were not tested.** This environment cannot resolve raw.githubusercontent.com. The upstream URLs are branch-based, but required content is pinned by SHA-256; future upstream changes can cause configure to stop.

## Publication hygiene checks

The distributed source contains no ROM/save/font/generated executable/archive files with the checked extensions, and no N64 ROM header signature was detected. It contains no user build log, build cache or local `.git` directory. The ignore rules were exercised against example build-cache, log, ROM, save, private-configuration and font-download paths. Root Actions workflow YAMLs are inactive reference copies rather than automatically enabled jobs.

Credential-pattern scanning flagged one existing embedded legacy MPQ signing key in `torch/lib/StormLib/src/SFileVerify.cpp`. Inspection established that this source file is byte-for-byte identical to the supplied baseline; it was retained and explicitly accounted for in the scan. All six bundled PEM files in libultraship are public-key files. The scan is limited pattern checking, not a security or licensing audit.

## Not verified

No GitHub account was authenticated, no repository was created remotely, and no public upload was performed. GitHub Desktop itself was not run here. The local Git import/clone checks demonstrate source layout, not the behaviour of a remote service.

No full-game Windows/MSVC compilation, installer execution, graphical launch, ROM extraction, save restoration, real LAN/two-PC gameplay, encryption/authentication or long-session stability test was performed. Enemy AI/boss health/projectiles/physics/cutscene replication remains outside the preview. No stable release or finished multiplayer certification is claimed.

## Check records

The repository preparation check ran 38 named assertions/operations; their outcomes are recorded in `CHECK_RESULTS.json`. Those include Git operations as well as content assertions; they are not 38 gameplay tests.

`SOURCE_MANIFEST.json` records provenance and changes relative to the source preview. `SOURCE_FILES.sha256` at the repository root records the final distributed file bytes (excluding itself). Git can normalize text line endings on import/checkout, so those hashes describe the ZIP's contents, not every possible working-tree line-ending configuration.

The archive's source-only layout leaves the user's existing local build and its cache untouched.
