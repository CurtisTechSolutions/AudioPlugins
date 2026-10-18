# Contributing

## Workflow

1. Branch from `main`, open a pull request.
2. CI builds and tests every plugin on Windows, macOS and Linux and runs
   pluginval at strictness 10. Download the VST3 zips from the run's
   **Artifacts** section to try a change in a DAW.
3. Squash-merge. The PR title becomes the commit message on `main`.

## PR titles: Conventional Commits

The **PR title** check enforces this, because release-please reads `main`'s
commit messages to decide the next version and write the changelog.

| Title | Effect on the next release |
|---|---|
| `feat: …` / `feat(frisson): …` | minor bump (0.1.0 → 0.2.0), listed under Features |
| `fix: …` | patch bump (0.2.0 → 0.2.1), listed under Bug Fixes |
| `perf: …` | patch bump, listed under Performance |
| `feat!: …` or a `BREAKING CHANGE:` footer | major bump once we are ≥ 1.0 (minor while 0.x) |
| `docs:` `ci:` `build:` `test:` `refactor:` `chore:` | no release on their own, hidden from the changelog |

Scopes are optional; use the plugin name when the change is specific to one
(`fix(intone): …`).

## Releases

Nothing to do by hand:

1. After a `feat`/`fix`/`perf` lands on `main`, release-please opens or updates a
   PR titled `chore(main): release x.y.z` that bumps `version.txt` and
   `CHANGELOG.md`.
2. Merging that PR tags `vx.y.z` and publishes the GitHub release.
3. The **Release** workflow then builds optimised (LTO) binaries for all three
   platforms and attaches `AudioPlugins-x.y.z-{windows,macos,linux}.zip` and
   `SHA256SUMS.txt` to the release.

To force a specific version, put `Release-As: 1.0.0` in a commit body. To
rebuild the assets of an existing release (e.g. after adding signing secrets),
run **Actions → Release → Run workflow** with the tag.

`version.txt` is the only place the version lives; CMake reads it, so every
plugin reports the release version to the host.

## Secrets (all optional)

Repository or organisation secrets, under *Settings → Secrets and variables → Actions*:

| Secret | Used for |
|---|---|
| `MY_RELEASE_PLEASE_TOKEN` | PAT (contents + pull-requests write) so release PRs trigger CI; without it the built-in token is used, which requires *Settings → Actions → Allow GitHub Actions to create and approve pull requests* |
| `MACOS_CERT_P12_BASE64` | Developer ID Application certificate, `.p12` exported from Keychain, `base64 -i cert.p12` |
| `MACOS_CERT_PASSWORD` | Password of that `.p12` |
| `APPLE_ID`, `APPLE_TEAM_ID`, `APPLE_APP_PASSWORD` | Notarisation (app-specific password from appleid.apple.com) |

Without the macOS secrets the bundles are ad-hoc signed and users clear the
quarantine flag once (see `packaging/INSTALL.txt`). With the certificate only,
they are Developer-ID signed with the hardened runtime; with all of them they
are notarised and stapled.

## Building locally

See the README. Useful options: `-DAP_ENABLE_LTO=OFF` for fast iteration,
`-DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE` to build offline.
