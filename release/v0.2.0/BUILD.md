# v0.2.0 release binaries

## Linux x86_64

Built on Ubuntu 22.04, GCC 11, Release (`-DCMAKE_BUILD_TYPE=Release`).

```
release/v0.2.0/linux-x86_64/byzed
release/v0.2.0/linux-x86_64/byze-cli
release/v0.2.0/linux-x86_64/byze-wallet
release/v0.2.0/linux-x86_64/byze-tx
release/v0.2.0/linux-x86_64/byze-util
```

Verify: `./byzed --version` → `v0.2.0`

## Windows / macOS

### macOS arm64 (Apple Silicon)

Built natively on macOS 26, Apple Clang, Qt 6.11, Release.

```bash
./release/v0.2.0/package-macos-arm64.sh
export SIGN_IDENTITY='Developer ID Application: Your Name (TEAMID)'
./release/v0.2.0/sign-macos-release.sh
# Optional notarization (requires Xcode + notarytool profile):
# NOTARIZE=1 ./release/v0.2.0/sign-macos-release.sh
```

Artifacts:

```
release/byze-core-0.2.0-macos-arm64.dmg          # GUI installer (drag to Applications)
release/v0.2.0/macos-arm64/byzed               # CLI daemon
release/v0.2.0/macos-arm64/byze-cli
release/v0.2.0/macos-arm64/byze-wallet
release/v0.2.0/macos-arm64/byze-tx
release/v0.2.0/macos-arm64/byze-util
release/v0.2.0/macos-arm64/Byze-Qt.app         # GUI app bundle
release/SHA256SUMS_Mac_arm64.txt
```

Intel macOS (`x86_64`) was built separately via Guix cross-compile.

### Guix (all platforms)

Not produced on the Linux CI host (no mingw/osxcross/guix in path). Build with Guix:

```bash
git checkout release/v0.2.0
./contrib/guix/guix-build 0.2.0
```

Artifacts under `guix-build-0.2.0/output/`.

## Checksums

Generate after final binaries are frozen:

```bash
cd release/v0.2.0/linux-x86_64
sha256sum byze* > SHA256SUMS
```
