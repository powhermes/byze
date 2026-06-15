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
