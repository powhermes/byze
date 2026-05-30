#!/bin/bash
# Repackage an existing build-windows/ tree into byze-<version>-win64.zip
set -e
cd "$(dirname "$0")"
VERSION="${CLIENT_VERSION:-0.1.0}"
DIST="build-windows/dist/byze-${VERSION}-win64"
rm -rf build-windows/dist
mkdir -p "$DIST"
cp build-windows/bin/byze-qt.exe build-windows/bin/byzed.exe build-windows/bin/byze-cli.exe build-windows/bin/byze.exe "$DIST/"
windeployqt6 --release --no-translations "$DIST/byze-qt.exe"
for dll in libevent-2-1-7.dll libevent_core-2-1-7.dll libevent_extra-2-1-7.dll libsqlite3-0.dll libqrencode-4.dll; do
  if [ -f "/mingw64/bin/${dll}" ]; then
    cp "/mingw64/bin/${dll}" "$DIST/"
  fi
done
cat > "$DIST/README.txt" << READMEEOF
Byze Core ${VERSION} (Windows 64-bit)
=====================================

Unpack this folder anywhere and run byze-qt.exe to start the GUI wallet.

Included tools:
  byze-qt.exe  - GUI wallet
  byzed.exe    - Full node daemon
  byze-cli.exe - Command-line RPC client
  byze.exe     - Multi-call RPC client

More info: https://github.com/powhermes/byze
READMEEOF
cd build-windows/dist
rm -f "byze-${VERSION}-win64.zip"
zip -r "byze-${VERSION}-win64.zip" "byze-${VERSION}-win64"
echo "=== Archive ==="
ls -lh "byze-${VERSION}-win64.zip"
echo "=== Contents ==="
ls -lh "byze-${VERSION}-win64/" | head -30
