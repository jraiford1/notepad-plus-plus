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
| **Cut** (`C` item) | A `macos-v<upstream version>[-<n>]` tag on `master`; its product is the zipped `Notepad++.app` CI builds for that tag (see Q1) |
| **Distribution** (`X` item) | That CI artifact, for the owner's own use; nothing is published publicly (see Q1) |
| **Change ID** | None |

## How release artifacts are produced

No `cut_script`: CI does it. Every push builds `macos` in Release, runs `ctest` and uploads
`Notepad++-macOS.zip` (the app, ad-hoc signed) as the run's artifact; for a cut, that is the run of the tag's
commit. Locally the same is `macos/ci/build-and-test.sh` then `ditto -c -k --keepParent Notepad++.app
Notepad++-macOS.zip`. Signing with an Apple Developer identity is the owner's step if ever wanted; until then
the app is opened the first time with right-click → Open.

## Verification checks and tooling

- `macos/ci/build-and-test.sh` is the check: it configures and builds `macos` (Release, universal arm64 + x86_64, macOS 13.3 and
  later), checks that this branch changed nothing in upstream's directories since the last upstream
  merge, and runs `ctest`: the engine's rule tests, the runtime's CRT tests and the end-to-end smoke test
  (`macos/ci/smoke-test.sh`: open, edit, save, Save As, Find, dialogs and panels, a plugin, dark mode),
  on the arm64 slice and, through Rosetta, the x86_64 slice of the universal app.
  CI runs the same script on every push (`.github/workflows/macos.yml`).
- `NPPMAC_SKIP_GUI_TESTS=1` leaves out the smoke test where there is no window server.
- The engine is deterministic: two runs over the same upstream tree give byte-identical output.
- Upstream is merged nightly by `.github/workflows/macos-upstream-sync.yml`, which builds and tests the
  merge and opens a pull request to `master`.
- Test runs of the app use `-settingsDir=<folder>`, never the real settings.

## Q1

**What does a cut produce for this project, and what does distribution mean?**

Answered (deduced from the owner's direction, "we are just a private branch"; easily changed): a cut tags
`master` as `macos-v<upstream Notepad++ version>[-<n>]`, and its product is the zipped `Notepad++.app` CI
builds for that commit. Distribution is that artifact, for the owner's own use; nothing is published publicly.
Pushing a tag is the owner's step.

## Q2

**What should the Mac app be called, with which bundle identifier and icon?**

Answered (owner): it is Notepad++. This is the same project, and the fork is a private branch of it, so the app
keeps upstream's name and icon. `NPPMAC_APP_NAME`, `NPPMAC_BUNDLE_ID` and `NPPMAC_APP_ICON` stay as optional
CMake settings with those defaults. Plugins live where upstream puts them (`plugins/` next to the program).
