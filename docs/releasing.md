# Releasing

Releases are GitHub Releases holding an APK signed with the project's release key. Every push to
`main` still builds a debug-signed APK as a CI artifact; only a `v*` tag makes a release.

## Cutting a release

```sh
tools/release.sh 0.2.0
```

The script checks that `main` is clean and in sync with `origin`, sets `VERSION` in
`CMakeLists.txt` (the version's only home), commits `release: v0.2.0`, tags `v0.2.0` and pushes
both at once. Releasing the version already in `CMakeLists.txt` tags it without a commit.

CI (`.github/workflows/ci.yml`) then runs as for any push, plus, for the tag:

1. **Version check:** the tag must match `VERSION`, or the build fails.
2. **Signing:** the release keystore is decoded from the `SLOPTUBE_KEYSTORE_BASE64` secret and
   the APK is signed with it. The password reaches `apksigner` through the environment
   (`SLOPTUBE_KEYSTORE_PASS=env:SLOPTUBE_KEYSTORE_PASSWORD`), never through the build files.
3. **Publishing:** once formatting, the CLI and the APK pass, the `release` job publishes
   `SlopTube-v0.2.0.apk` and its SHA-256 as a GitHub Release, with notes generated from the
   commits since the previous tag.

The version code is `MAJOR * 10000 + MINOR * 100 + PATCH`, so minor and patch stay below 100 and
every release must be newer than the last: Android refuses to downgrade.

## Release key (once)

```sh
tools/release-key.sh
```

It creates a 4096-bit RSA key in a PKCS12 keystore, with a random password, under
`~/.config/studio.kanye.sloptube/`, and stores both as the repository secrets
`SLOPTUBE_KEYSTORE_BASE64` and `SLOPTUBE_KEYSTORE_PASSWORD` with the GitHub CLI.

Back up the keystore and its password. Android only installs an update signed with the same key:
losing it means every user has to uninstall before moving to a build signed with a new one.

Release APKs and the debug APKs of CI artifacts are signed with different keys, so going from
one to the other also needs an uninstall (which clears accounts and downloads).

## Signing locally

The packaging step signs with `SLOPTUBE_KEYSTORE` (default: `~/.android/debug.keystore`, created
when missing) and `SLOPTUBE_KEYSTORE_PASS`, an `apksigner` password spec (`pass:…`, `env:…` or
`file:…`; default `pass:android`):

```sh
cmake --preset android-release \
  -DSLOPTUBE_KEYSTORE="$HOME/.config/studio.kanye.sloptube/release.keystore" \
  -DSLOPTUBE_KEYSTORE_PASS="file:$HOME/.config/studio.kanye.sloptube/release.keystore.password"
cmake --build --preset android-release
```
