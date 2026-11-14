---
name: vitals-dev
description: Develop, test, package and release Vitals, the Omarchy system monitor in this repository, and keep it compliant with Omarchy's package repository (omacom/omarchy-pkgs). Use for any change to Vitals' code, QML, translations, packaging, screenshots, README, version, or release. Triggers: vitals, package-check, omarchy-pkgs, release, bump the version, PKGBUILD, screenshots.
---

# Developing Vitals

Vitals follows Omarchy's own apps (Omacut, Hype, Monologue): C++ and Qt Quick,
qmake6, one binary, theme and language followed live. The global
`omarchy-app` skill holds the general conventions; this one holds what is
specific to Vitals.

## Layout

- `src/` flat: `monitor.*` (sampling, rewind, pause/limit), `processmodel.*`
  (rows and roles for QML), `procfs.*` (parsers), `apps.*` (cgroups and
  desktop names), `run.*` (`vitals run`), `cli.*` (commands), `theme.*`,
  `translator.*`, QML panels, `i18n/{en,nl}.json`.
- `tests/vitals_tests.cpp`: Qt Test, compiles `../src/*.cpp` directly. A new
  source file goes in `vitals.pro` and `tests/vitals_tests.pro`.
- `pkgbuild/PKGBUILD`: builds this checkout for `bin/install`.
- `pkgbuild/omarchy-pkgs/`: the recipe for omacom/omarchy-pkgs, built from
  the GitHub release tarball. It is the source of truth for the PR there.
- `tools/demo/`: the made-up demo system and the screenshot driver.

## Loop

1. `bin/build`, then try `build/vitals open` or a CLI command.
2. `bin/test` after every backend change. All tests must pass.
3. Every visible string goes through `qsTr`/`tr` and gets a Dutch entry in
   `src/i18n/nl.json`; `shippedCatalogsCoverEveryString` fails otherwise.
4. `bin/package-check` before committing a packaging change, and before a
   release: it builds this checkout in omarchy-pkgs' own builder container
   (rootless Podman), runs `check()` (the test suite) there, and checks the
   result the way their CI does. New files must be `git add`ed to be included.
5. `bin/install` (needs a real terminal for sudo) to use the change.

## Rules

- Never change Omarchy or Hyprland config. Never run `vitals skill install`
  without the user's approval: it writes into `~/.agents` and `~/.claude`.
- Pause and limit act only on `app.slice` and `background.slice` units, never
  on `session.slice`, system services, or the unit Vitals runs in. Keep the
  undo-on-close behaviour of the window.
- Screenshots and README examples come from the demo system, never from the
  user's machine: `tools/demo/screenshots.py` under
  `systemd-inhibit --what=idle`, hands off the mouse for four minutes.
- Commit messages are short and imperative.

## Version and release

The version is set in `vitals.pro` (`VERSION = x.y.z`) and as `pkgver` in both
PKGBUILDs; `bin/package-check` refuses a mismatch.

1. Set the version in the three places; reset `pkgrel=1`. Update README if
   behaviour changed. `bin/test`, `bin/package-check`. Commit.
2. Push, then `git tag -a vX.Y.Z -m "Vitals X.Y.Z"`, push the tag, and
   `gh release create vX.Y.Z --verify-tag` with short notes.
3. In `pkgbuild/omarchy-pkgs/`, run `updpkgsums` (delete the downloaded
   tarball afterwards), then `bin/package-check --release` to build exactly
   what will ship. Commit the new checksum.
4. Once Vitals is in omarchy-pkgs, its `upstream.watch` picks up the GitHub
   release on its own and a bot opens the update PR; nothing else is needed.

## omarchy-pkgs compliance

What their README and CI require, and what the recipe already does:

- One package per PR, branched from `master`, files only under
  `pkgbuilds/vitals/`: `PKGBUILD`, `vitals.install`, `.omarchy/package.json`.
  The PR says what the package is and how it was tested.
- `arch=('x86_64' 'aarch64')`; source pinned to a release tag with its
  sha256; every file owned by root (`install -D`, no vendor tarballs).
- `.omarchy/package.json`: `"source": "local"`, `"auto_merge": true`, and an
  `upstream.watch` on `carroarmato0/vitals` with pattern
  `v(?P<version>[0-9]+(?:\.[0-9]+)*)`. Check it with
  `python build/omarchy-pkgs/helpers/upstream-watch.py check build/omarchy-pkgs/pkgbuilds/vitals`.
- Not on the fast ring: it is compiled here and links Qt, which their
  `bin/check-fast-ring` refuses. It reaches rc and stable by promotion.
- A packaging change at the same version bumps `pkgrel`, or it never ships.
  Never set `epoch`.
- A non-draft PR that only touches `pkgbuilds/` merges itself once green, so
  open it as a draft unless the user says otherwise. Outside contributors'
  builds wait for a maintainer's `build-approved` label.
- Opening a PR, pushing, tagging, and releasing are outward-facing: confirm
  with the user first.
