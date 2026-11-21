# Release checklist

Nothing in this checklist publishes a release. Checked items were verified on the local tree after `project(VERSION)` was set to 1.0.0, or against the named Actions run.

## Completed

### Code

- [x] GCC Debug build passes, with no compiler warnings. `./build/debug/incidentlab` prints `Version 1.0.0`.
- [x] GCC Release build passes, with no compiler warnings. `./build/release/incidentlab` prints `Version 1.0.0`.
- [x] GUI build passes. `incidentlab_gui` linked. The window does not display a version.
- [x] No known critical simulation bug in the suites that ran.

### Tests

- [x] All eight CTest suites pass in Debug and Release.
- [x] GCC AddressSanitizer and UndefinedBehaviorSanitizer pass with leak detection on.
- [x] Coverage regenerated after the version change: 84.3% lines (1472/1747) and 91.8% functions (167/182) for `incidentlab_core`.
- [x] GitHub Actions on `92db659` is green for GCC Debug, GCC Release, Clang Debug, GCC ASan and UBSan, and GCC coverage. That commit does not contain the 1.0.0 version change.

### Documentation

- [x] README, architecture, OOP design, user guide, and build guide agree on the commands and the version source.
- [x] Screenshot files exist and the README links resolve. The captures show the running window with readable text.
- [x] Release notes stay marked unpublished.
- [x] Changelog records `[1.0.0] — Unreleased`.

### Repository

- [x] `.gitignore` excludes build trees.
- [x] No credentials found in the tracked files reviewed for this pass.
- [x] No generated binaries are tracked. The DejaVu files and `assets/fonts/LICENSE` stay tracked.
- [x] Internal Markdown links in the documentation resolve.
- [x] MIT License is in the repository root. Copyright (c) 2026 Inga Gaidukove. `assets/fonts/LICENSE` is unchanged.

## Pending user approval

- [ ] Commit the version change, the MIT License, and the documentation updates.
- [ ] Push that commit.
- [ ] Wait until GitHub Actions is green on that exact commit.
- [ ] Create the annotated tag `v1.0.0` manually.
- [ ] Publish the GitHub Release manually from that tag, using `docs/release-notes-v1.0.0.md`.
- [ ] Set the GitHub description and topics listed in `docs/portfolio-description.md`.

## Blocked

No technical blocker was found in the builds run for this preparation. Publication stays blocked on the approvals above, not on a failing build.
