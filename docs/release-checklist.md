# Release checklist

Checked items were verified in the release-check environment on the uncommitted documentation tree, or against the public Actions run named in the item. Unchecked items are not done.

The binary version is still 0.1.0. Nothing in this checklist publishes a release.

## Code

- [x] Debug build passes.
- [x] Release build passes.
- [x] GUI build passes with a local raylib install.
- [x] No compiler warnings in those four local builds.
- [x] No known critical simulation bug in the suites that ran.

## Tests

- [x] All eight CTest suites pass in Debug and Release.
- [x] GCC AddressSanitizer and UndefinedBehaviorSanitizer pass with leak detection on.
- [x] Coverage generated: 84.3% lines (1472/1747) and 91.8% functions (167/182) for `incidentlab_core`.
- [ ] GitHub Actions green on the release-candidate tree. The last pushed commit, `36ae182`, is green. This documentation has not been pushed.

## Documentation

- [x] README written for a first-time reader.
- [x] Architecture documented.
- [x] OOP design documented from the real types.
- [x] User guide complete for the console flags and the window controls that exist.
- [x] Build guide complete for the verified commands.
- [x] Screenshots captured from the running window and stored in `docs/screenshots/`.
- [x] Demo script written against those controls and the locked metrics.

## Repository

- [x] `.gitignore` excludes build trees, objects, and coverage data.
- [x] No credentials found in the tracked files reviewed for this pass.
- [x] No generated binaries are tracked. Fonts and their license are tracked on purpose.
- [ ] Project license reviewed and chosen. There is no `LICENSE` file. DejaVu's notice is present.
- [ ] GitHub description and topics applied by the repository owner. Suggested text is in `docs/portfolio-description.md`.

## Release

- [ ] Compiled version set to 1.0.0. It is 0.1.0 until you approve the bump.
- [x] Changelog drafted as `[1.0.0] — Unreleased`.
- [x] Release-candidate notes drafted. They do not say the release is published.
- [ ] You approve publication.
- [ ] Git tag created manually after that approval.
- [ ] GitHub Release published manually after that approval.
