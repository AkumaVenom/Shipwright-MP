# Publish this source — no compilation required

Use this separate source snapshot, not the large folder in which you compiled the game. Keep that working folder and its `_beginner_build` cache untouched. These instructions create a **new repository**; they do not replace an existing GitHub project or preserve an upstream fork's commit history.

## GitHub Desktop steps

1. Extract this ZIP with **Extract All**. Open the inner `Shipwright-MP-GitHub` folder. You should see `README.md`, `CMakeLists.txt`, `soh`, `torch`, `libultraship` and `build-helper` together.
2. Open GitHub Desktop and sign in. Choose **File → New repository**. Name it `Shipwright-MP` (or another unused name), and leave the local path at a separate location such as your normal Documents/GitHub folder. Leave **Initialize this repository with a README** unchecked; select **None** for Git ignore and License. This snapshot already has its own README/ignore rules and preserves existing licences. Click **Create repository**. This is local only.
3. Open the newly created repository folder in File Explorer. Copy **everything inside** the extracted `Shipwright-MP-GitHub` folder into it, including the dot-named files/folders. Do not put the entire outer folder or ZIP inside the repository. `CMakeLists.txt` and `README.md` must sit at the repository's top level. Preserve the new repository's `.git` folder. Replacement of an automatically created `.gitattributes` is expected in this new empty repository; do not overwrite an existing project.
4. Back in GitHub Desktop, review **Changes**. The repository should include game source and both dependency folders, not `_beginner_build`, `BUILD_LOGS`, `OUTPUT`, ROMs, saves or compiled files. Use the summary **Import consolidated Direct-IP source preview**, then click **Commit to main** (or the branch name shown). This can take a little time for the large number of small source files; it does not compile anything.
5. Click **Publish repository**. Review the account/name. To make the source public, **untick Keep this code private**, then click **Publish Repository**. Open **Repository → View on GitHub** to check the result.

Do not use GitHub's browser file uploader for the ZIP as your repository's only file; that stores an archive rather than a navigable source tree. Do not copy files from your used build folder over this source snapshot. Do not choose a new blanket MIT or other licence for all inherited code.

## Already have a repository or fork?

Do not use the new-repository steps to replace it. Keep its `.git` history, branches, remote and licences. Integrating this snapshot into an existing fork needs a reviewed diff against that exact fork; the clean snapshot is not proof of a matching upstream commit.

## What to tell visitors

Suggested description: **Unofficial experimental Shipwright direct-IP host/join source preview. Full-game and multiplayer validation pending.**

Publish this as source under development, not as a finished compiled release. The multiplayer exclusions are listed at the top of `README.md`.

## Official reference

GitHub Desktop's documented create/commit/publish workflow:
https://docs.github.com/en/desktop/overview/creating-your-first-repository-using-github-desktop

Existing repository publishing and the public/private checkbox:
https://docs.github.com/en/desktop/adding-and-cloning-repositories/adding-an-existing-project-to-github-using-github-desktop
