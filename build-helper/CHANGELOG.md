# Windows build helper history

## Repository packaging revision

Includes helper 1.2 directly. Only the missing-source diagnostic is updated to refer to the consolidated repository rather than the superseded ZIP-preparation step. No installer, build, or resume behaviour is changed by this diagnostic edit.

## 1.2

Retains 1.1 existing-install SDK repair and adds opt-in cache-preserving resume via `4_RESUME_BUILD.cmd`. Includes AnchorClientState typedef naming and the OptionValue accessor correction in the game source.

## 1.1

Checks SDK files and modifies an existing Visual Studio installation when components are missing, instead of trying to reinstall it with WinGet.

## Validation boundary

This script has not been executed on Windows in the packaging environment. The full game build, installer and multiplayer gameplay are not certified by the source package.
