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
  listed in `macos/upstream-changes.txt` until upstream takes it (that file says what to do when an
  upstream merge meets one). The pull request upstream is the owner's.
- **Who consumes a release.** The owner only, on their own Mac. The fork publishes no builds, and GitHub
  Actions are switched off on it (W43): the app is built and tested on the owner's Mac.

## What the process words mean here

| Word | Meaning in this project |
|---|---|
| **Change** (a `CHANGE` item) | A code change to the macOS build (or to shared files it uses), committed to `master` as `W<n>: ...` |
| **Working copy / increment** | A branch per `W` item, merged to `master` |
| **APPLIED** | Merged to `master`; the macOS build (`cmake -S macos`) succeeds and its tests (`ctest`) pass |
| **Cut** (`C` item) | A `macos-v<upstream version>[-<n>]` tag on `master`; its product is the `Notepad++.app` built from that tag on the owner's Mac (see Q1) |
| **Distribution** (`X` item) | That app, on the owner's Mac only; no build is published (see Q1) |
| **Change ID** | None |

## How release artifacts are produced

No `cut_script`. A cut's app is built on the owner's Mac from the tag: the native build (see below), in
Release. GitHub Actions are switched off on the fork (W43), so nothing is built or kept there. The workflow
files stay as they are, including W42's step that keeps master and tag builds in a private repository
(on hold; it needs the fork's variable `BUILDS_REPO` and secret `BUILDS_TOKEN`).

## Verification checks and tooling

- `macos/ci/build-and-test.sh macos/build` is the check: it configures and builds `macos` in Release (the
  project's default is universal arm64 + x86_64, macOS 13.3 and later; on the owner's Mac, native, see below), checks that this branch changed nothing in upstream's directories since the last upstream
  merge beyond the commits in `macos/upstream-changes.txt`, and runs `ctest`: the engine's rule tests, the runtime's CRT tests and the end-to-end smoke test
  (`macos/ci/smoke-test.sh`: open, edit, save, Save As, Find, an input method, printing, dialogs and
  panels, a plugin, dark mode),
  on the arm64 slice and, in a universal build, the x86_64 slice through Rosetta. The smoke test also
  covers files from Finder, undo and copy/paste keys, outside changes, quitting, restarting and the
  session.
- GUI tests run on the owner's Mac, in the foreground, with the owner's own Notepad++ quit first.
- On the owner's Mac, only the native build (configured with `-DCMAKE_OSX_ARCHITECTURES=arm64`): no
  universal build and no x86_64 pass under Rosetta. With CI off, nothing builds or tests the Intel half.
- GitHub Actions are switched off on the fork (W43): all four workflows (`macos.yml`,
  `macos-upstream-sync.yml`, and upstream's CI_build and Plugin Files Release Notifier) are disabled as a
  workflow setting (`gh workflow disable`), with their files untouched. Upstream's two had run Windows jobs
  on every push, and GitHub disabled Actions on the fork to cut wasted compute (W38).
- `NPPMAC_SKIP_GUI_TESTS=1` leaves out the smoke test where there is no window server.
- The engine is deterministic: two runs over the same upstream tree give byte-identical output.
- Upstream is merged on the owner's Mac, as `.github/workflows/macos-upstream-sync.yml` did nightly:
  `upstream/master` merged into a branch, the check run on it, then merged to `master`.
- Test runs of the app use `-settingsDir=<folder>`, never the real settings.

## Q1

**What does a cut produce for this project, and what does distribution mean?**

Answered (deduced from the owner's direction, "we are just a private branch"; easily changed): a cut tags
`master` as `macos-v<upstream Notepad++ version>[-<n>]`, and its product is the `Notepad++.app` built
from that commit on the owner's Mac. Distribution is that app, for the owner's own use. No build is published:
no GitHub release and no CI artifact (the owner, 2026-11-10 and 2026-11-11; W41, W43). Pushing a tag is the
owner's step.

## Q2

**What should the Mac app be called, with which bundle identifier and icon?**

Answered (owner): it is Notepad++. This is the same project, and the fork is a private branch of it, so the app
keeps upstream's name and icon. `NPPMAC_APP_NAME`, `NPPMAC_BUNDLE_ID` and `NPPMAC_APP_ICON` stay as optional
CMake settings with those defaults. Plugins live where upstream puts them (`plugins/` next to the program).
