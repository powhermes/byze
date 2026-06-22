#!/usr/bin/env bash
# Package Byze v0.2.1 macOS arm64 release (CLI + GUI + DMG)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD="${ROOT}/build"
OUT="${ROOT}/release/v0.2.1/macos-arm64"
DMG="${ROOT}/release/byze-core-0.2.1-macos-arm64.dmg"
QT_BASE="$(brew --prefix qtbase 2>/dev/null || echo /opt/homebrew/opt/qtbase)"
QT_TRANSLATIONS="$(brew --prefix qttranslations 2>/dev/null || echo /opt/homebrew/opt/qttranslations)/share/qt/translations"
QTDIR="${QTDIR:-$QT_BASE}"

echo "==> Configure (Release + GUI)"
cmake -S "${ROOT}" -B "${BUILD}" -DCMAKE_BUILD_TYPE=Release -DBUILD_GUI=ON \
  -DCMAKE_PREFIX_PATH="/opt/homebrew/opt/qt@6;/opt/homebrew/opt/qtbase;/opt/homebrew/opt/qttools" \
  -DQt6LinguistTools_DIR="/opt/homebrew/opt/qttools/lib/cmake/Qt6LinguistTools"

echo "==> Build"
cmake --build "${BUILD}" -j"$(sysctl -n hw.ncpu)"

echo "==> Deploy Qt app bundle"
cd "${BUILD}"
rm -rf dist
QTDIR="${QTDIR}" python3 "${ROOT}/contrib/macdeploy/macdeployqtplus" Bitcoin-Qt.app \
  -no-strip \
  -translations-dir="${QT_TRANSLATIONS}"

echo "==> Re-sign app bundle (macdeploy invalidates nested signatures)"
"${ROOT}/release/v0.2.1/resign-macos-app.sh" "${BUILD}/dist/Bitcoin-Qt.app" "-"

echo "==> Stage release directory"
rm -rf "${OUT}"
mkdir -p "${OUT}"
cp "${BUILD}/bin/byzed" "${BUILD}/bin/byze-cli" "${BUILD}/bin/byze-wallet" \
   "${BUILD}/bin/byze-tx" "${BUILD}/bin/byze-util" "${OUT}/"
cp -R "${BUILD}/dist/Bitcoin-Qt.app" "${OUT}/Byze-Qt.app"

echo "==> Create DMG"
STAGE="$(mktemp -d)"
trap 'rm -rf "${STAGE}"' EXIT
cp -R "${OUT}/Byze-Qt.app" "${STAGE}/"
ln -s /Applications "${STAGE}/Applications"
hdiutil create -volname "Byze Core v0.2.1 arm64" -srcfolder "${STAGE}" -ov -format UDZO "${DMG}"

echo "==> Checksums"
(
  cd "${ROOT}/release"
  shasum -a 256 "$(basename "${DMG}")" > SHA256SUMS_Mac_arm64.txt
  cd v0.2.1/macos-arm64
  shasum -a 256 byze* > SHA256SUMS
)

echo ""
echo "Done."
echo "  CLI + app: ${OUT}/"
echo "  DMG:       ${DMG}"
echo ""
echo "Sign before publishing:"
echo "  ${ROOT}/release/v0.2.1/sign-macos-release.sh"
