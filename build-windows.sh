#!/bin/bash
# Byze Windows 64-bit build and packaging script for MSYS2 MINGW64
#
# Prerequisites:
#   1. Install MSYS2 from https://www.msys2.org/
#   2. Open the 64-bit MinGW shell: C:\msys64\msys2_shell.cmd -mingw64
#   3. Run: ./build-windows.sh
#
# This script installs pacman dependencies, configures CMake with wallet + GUI,
# builds all release binaries, bundles Qt DLLs via windeployqt6, and produces:
#   build-windows/dist/byze-<version>-win64.zip
set -e
if [[ "$MSYSTEM" != "MINGW64" ]]; then
    echo "Error: Run this from MSYS2 MINGW64 (C:\\msys64\\msys2_shell.cmd -mingw64)"
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

VERSION="${CLIENT_VERSION:-0.1.0}"
BUILD_DIR="build-windows"
DIST_NAME="byze-${VERSION}-win64"
DIST="${BUILD_DIR}/dist/${DIST_NAME}"

echo "=========================================="
echo "Byze Windows Build (${VERSION})"
echo "=========================================="

# Install build dependencies if missing
pacman -S --needed --noconfirm \
    mingw-w64-x86_64-cmake \
    mingw-w64-x86_64-gcc \
    mingw-w64-x86_64-make \
    mingw-w64-x86_64-pkg-config \
    mingw-w64-x86_64-boost \    mingw-w64-x86_64-libevent \
    mingw-w64-x86_64-sqlite3 \
    mingw-w64-x86_64-qrencode \
    mingw-w64-x86_64-qt6-base \
    mingw-w64-x86_64-qt6-tools \
    git \
    zip

# Initialize submodules if needed
if [ -d .git ]; then
    git submodule update --init --recursive || true
fi

# Configure
cmake -B "$BUILD_DIR" -G "MinGW Makefiles" \
    -DCMAKE_BUILD_TYPE=Release \
    -DENABLE_WALLET=ON \
    -DBUILD_GUI=ON \
    -DENABLE_IPC=OFF \
    -DBUILD_TESTS=OFF \
    -DBUILD_GUI_TESTS=OFF \
    -DBUILD_BENCH=OFF \
    -DBUILD_FUZZ_BINARY=OFF

# Build
cmake --build "$BUILD_DIR" -j"$(nproc)"

# Package
rm -rf "${BUILD_DIR}/dist"
mkdir -p "$DIST"

cp "${BUILD_DIR}/bin/byze-qt.exe" "$DIST/"
cp "${BUILD_DIR}/bin/byzed.exe" "$DIST/"
cp "${BUILD_DIR}/bin/byze-cli.exe" "$DIST/"
cp "${BUILD_DIR}/bin/byze.exe" "$DIST/"

windeployqt6 --release --no-translations "$DIST/byze-qt.exe"

for dll in libevent-2-1-7.dll libevent_core-2-1-7.dll libevent_extra-2-1-7.dll libsqlite3-0.dll libqrencode-4.dll; do
    if [ -f "/mingw64/bin/${dll}" ]; then
        cp "/mingw64/bin/${dll}" "$DIST/"
    fi
done

cat > "$DIST/README.txt" << EOF
Byze Core ${VERSION} (Windows 64-bit)
=====================================

Unpack this folder anywhere and run byze-qt.exe to start the GUI wallet.

Included tools:
  byze-qt.exe  - GUI wallet
  byzed.exe    - Full node daemon
  byze-cli.exe - Command-line RPC client
  byze.exe     - Multi-call RPC client

More info: https://github.com/powhermes/byze
EOF

cd "${BUILD_DIR}/dist"
rm -f "${DIST_NAME}.zip"
zip -r "${DIST_NAME}.zip" "${DIST_NAME}"

echo ""
echo "Build complete!"
echo "  Binaries: ${SCRIPT_DIR}/${BUILD_DIR}/bin/"
echo "  Package:  ${SCRIPT_DIR}/${BUILD_DIR}/dist/${DIST_NAME}.zip"
echo "=========================================="
