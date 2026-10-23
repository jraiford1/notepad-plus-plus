# Notepad++ macOS — Tracker Notes

> This file holds this project's answers to the questions the work-tracker skill leaves open. Choices made in
> `work_config.json` (roles, planning, vcs, change IDs) are shown by `work.py version` and are not repeated here.

## What this project is

- **What we deliver.** A fork of Notepad++ (`jraiford1/notepad-plus-plus`) that adds a native macOS build
  alongside the unchanged Windows build.
- **Where they live.** Everything macOS-specific is in `macos/` (see `macos/README.md`): the migration engine,
  the Win32 runtime and the build. Upstream's tree (`PowerEditor/`, `scintilla/`, `lexilla/`, `boostregex/`)
  is never edited, so `upstream/master` keeps merging cleanly.
- **Who consumes a release.** Mac users of the fork, through a zipped `.app` attached to a GitHub release on the
  fork (the `X` step).

## What the process words mean here

| Word | Meaning in this project |
|---|---|
| **Change** (a `CHANGE` item) | A code change to the macOS build (or to shared files it uses), committed to `master` as `W<n>: ...` |
| **Working copy / increment** | A branch per `W` item, merged to `master` |
| **APPLIED** | Merged to `master`; the macOS build (`cmake -S macos`) succeeds and its tests (`ctest`) pass |
| **Cut** (`C` item) | Proposed, see Q1: a `macos-v<version>` tag on `master` and a zipped `Notepad++.app` |
| **Distribution** (`X` item) | Proposed, see Q1: publishing that zip as a GitHub release on the fork; the release tag identifies it |
| **Change ID** | None |

## How release artifacts are produced

No `cut_script` yet. Until there is one: configure and build `macos` in Release, run `ctest`, then
`ditto -c -k --keepParent Notepad++.app Notepad++-macos-<version>.zip`. Code signing and notarization need the
owner's Apple Developer identity and are a person's step; unsigned builds must be opened with right-click → Open.

## Verification checks and tooling

- `cmake -S macos -B macos/build && cmake --build macos/build` must succeed with no new warnings.
- `ctest --test-dir macos/build` runs the engine's rule tests (and, as they arrive, the runtime's tests).
- `cmake --build macos/build --target nppUpstream -- -k` must compile every upstream source file.
- The engine is deterministic: two runs over the same upstream tree give byte-identical output.
- Manual smoke test: launch the app, open a source file, check highlighting, find/replace, save, reopen.
- The Windows build is unaffected: `git diff upstream/master -- PowerEditor scintilla lexilla boostregex` is empty.

## Q1

**What does a cut produce for this project, and what does distribution mean?**

Proposed answer (awaiting the owner's confirmation; `Q1` stays `OPEN` until then): a cut tags `master` as
`macos-v<version>` and produces an unsigned, zipped `Notepad++.app` (universal arm64 + x86_64). Distribution is
publishing that zip as a GitHub release on `jraiford1/notepad-plus-plus`, identified by its tag.
