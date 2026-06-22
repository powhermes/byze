#!/usr/bin/env bash
# Build and package Byze v0.2.1 Linux x86_64 — includes byze-qt
# Run on the Vultr build server (Ubuntu/Debian with Qt6 dev libs)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="${ROOT}/build"
OUT="${ROOT}/release/v0.2.1/linux-x86_64"
TARBALL="${ROOT}/release/byze-core-0.2.1-linux-x86_64.tar.gz"

echo "==> Configure (Release + GUI)"
cmake -S "${ROOT}" -B "${BUILD}" -DCMAKE_BUILD_TYPE=Release -DBUILD_GUI=ON -DENABLE_IPC=OFF

echo "==> Build ($(nproc) cores)"
cmake --build "${BUILD}" -j"$(nproc)"

echo "==> Stage release directory"
rm -rf "${OUT}"
mkdir -p "${OUT}"
cp "${BUILD}/bin/byzed" \
   "${BUILD}/bin/byze-cli" \
   "${BUILD}/bin/byze-wallet" \
   "${BUILD}/bin/byze-tx" \
   "${BUILD}/bin/byze-util" \
   "${BUILD}/bin/byze-qt" \
   "${OUT}/"

# Copy Qt wallet readme
cat > "${OUT}/LINUX_QT_WALLET.md" <<'EOF'
# Byze Qt Wallet — Linux

Run with:
  ./byze-qt

Dependencies (Ubuntu/Debian):
  sudo apt install libqt6core6 libqt6widgets6 libqt6network6 libqt6dbus6

If Qt6 libs are not available, install via:
  sudo apt install qt6-base-dev
EOF

echo "==> Create tarball"
tar -czf "${TARBALL}" -C "${OUT}/.." "linux-x86_64"

echo "==> Checksums"
(
  cd "${ROOT}/release"
  sha256sum "$(basename "${TARBALL}")" > SHA256SUMS_Linux_x86_64.txt
  cd v0.2.1/linux-x86_64
  sha256sum byze* > SHA256SUMS
)

echo ""
echo "Done."
echo "  Binaries: ${OUT}/"
echo "  Tarball:  ${TARBALL}"
