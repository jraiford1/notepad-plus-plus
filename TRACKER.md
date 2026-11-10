# Notepad++ macOS — Tracker Notes

> This file holds this project's answers to the questions the work-tracker skill leaves open. Choices made in
> `work_config.json` (roles, planning, vcs, change IDs) are shown by `work.py version` and are not repeated here.

## What this project is

- **What we deliver.** A fork of Notepad++ (`jraiford1/notepad-plus-plus`) that adds a native macOS build
  alongside the unchanged Windows build.
- **Where they live.** Everything macOS-specific is in `macos/` (see `macos/README.md`): the migration engine,
  the Win32 runtime and the build. Upstream's tree (`PowerEditor/`, `scintilla/`, `lexilla/`, `boostregex/`)
  is not edited for macOS, so `upstream/master` keeps merging cleanly. The exception is job J3's changes,
  made for upstream itself: each is one commit on its own branch from upstream, merged into `master` and
  listed in `macos/upstream-changes.txt` until upstream takes it (that file says what to do when the
  nightly merge meets one). The pull request upstream is the owner's.
- **Who consumes a release.** The owner only. The fork publishes no builds: no GitHub release and no CI
  artifact. CI keeps its builds in a private repository that only the owner can see (W42).

## What the process words mean here

| Word | Meaning in this project |
|---|---|
| **Change** (a `CHANGE` item) | A code change to the macOS build (or to shared files it uses), committed to `master` as `W<n>: ...` |
| **Working copy / increment** | A branch per `W` item, merged to `master` |
| **APPLIED** | Merged to `master`; the macOS build (`cmake -S macos`) succeeds and its tests (`ctest`) pass |
| **Cut** (`C` item) | A `macos-v<upstream version>[-<n>]` tag on `master`; its product is the universal `Notepad++.app` CI builds for that tag, kept in the private builds repository (see Q1) |
| **Distribution** (`X` item) | That app, for the owner only; no build is published (see Q1) |
| **Change ID** | None |

## How release artifacts are produced

No `cut_script`: CI does it. Every push builds the universal app in Release and runs the tests; it uploads no
artifact and makes no release on the fork (W41). A push to `master` or a `macos-v*` tag then zips the app and
puts it in the private repository named by the fork's variable `BUILDS_REPO`, written with the secret
`BUILDS_TOKEN` (W42): `latest-master` holds the newest `master` build, replaced each time, and each tag gets a
release of its own. Without the variable the step is skipped.

## Verification checks and tooling

- `macos/ci/build-and-test.sh` is the check: it configures and builds `macos` (Release, universal arm64 + x86_64, macOS 13.3 and
  later), checks that this branch changed nothing in upstream's directories since the last upstream
  merge beyond the commits in `macos/upstream-changes.txt`, and runs `ctest`: the engine's rule tests, the runtime's CRT tests and the end-to-end smoke test
  (`macos/ci/smoke-test.sh`: open, edit, save, Save As, Find, an input method, printing, dialogs and
  panels, a plugin, dark mode),
  on the arm64 slice and, through Rosetta, the x86_64 slice of the universal app. The smoke test also
  covers files from Finder, undo and copy/paste keys, outside changes, quitting, restarting and the
  session.
- On the owner's Mac, only `ctest -LE gui`: GUI tests run on CI, on a pushed branch, merged when green.
  CI runs the same script on every push (`.github/workflows/macos.yml`).
- On the owner's Mac, only the native build (configured with `-DCMAKE_OSX_ARCHITECTURES=arm64`): no
  universal build and no x86_64 pass under Rosetta. The universal build and the Intel tests run on CI.
- On the fork, only the two macOS workflows run (`macos.yml`, `macos-upstream-sync.yml`). Upstream's
  inherited ones (CI_build, Plugin Files Release Notifier) stay disabled there as a workflow setting
  (`gh workflow disable`), with their files untouched: they ran Windows jobs on every push that the
  fork does not need, and GitHub disabled Actions on the fork to cut wasted compute (W38).
- `NPPMAC_SKIP_GUI_TESTS=1` leaves out the smoke test where there is no window server.
- The engine is deterministic: two runs over the same upstream tree give byte-identical output.
- Upstream is merged nightly by `.github/workflows/macos-upstream-sync.yml`, which builds and tests the
  merge and opens a pull request to `master`.
- Test runs of the app use `-settingsDir=<folder>`, never the real settings.

## Q1

**What does a cut produce for this project, and what does distribution mean?**

Answered (deduced from the owner's direction, "we are just a private branch"; easily changed): a cut tags
`master` as `macos-v<upstream Notepad++ version>[-<n>]`, and its product is the universal
`Notepad++.app` CI builds for that tag, kept in the private builds repository. Distribution is that app, for the
owner's own use. No build is published: no GitHub release and no CI artifact on the fork (the owner,
2026-11-10; W41, W42). Pushing a tag is the owner's step.

## Q2

**What should the Mac app be called, with which bundle identifier and icon?**

Answered (owner): it is Notepad++. This is the same project, and the fork is a private branch of it, so the app
keeps upstream's name and icon. `NPPMAC_APP_NAME`, `NPPMAC_BUNDLE_ID` and `NPPMAC_APP_ICON` stay as optional
CMake settings with those defaults. Plugins live where upstream puts them (`plugins/` next to the program).
