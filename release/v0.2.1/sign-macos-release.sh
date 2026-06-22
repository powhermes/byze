#!/usr/bin/env bash
# Sign (and optionally notarize) Byze macOS arm64 release artifacts.
#
# Prerequisites:
#   1. Developer ID Application cert in Keychain (NOT iOS Distribution)
#      security find-identity -v -p codesigning
#   2. For notarization: Xcode + notarytool credentials
#
# Usage:
#   export SIGN_IDENTITY='Developer ID Application: Your Name (TEAMID)'
#   ./sign-macos-release.sh
#   NOTARIZE=1 ./sign-macos-release.sh

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
APP="${ROOT}/release/v0.2.1/macos-arm64/Byze-Qt.app"
DMG="${ROOT}/release/byze-core-0.2.1-macos-arm64.dmg"
IDENTITY="${SIGN_IDENTITY:-}"

if [[ -z "${IDENTITY}" ]]; then
  IDENTITY="$(security find-identity -v -p codesigning | sed -n 's/.*"\(Developer ID Application:.*\)"/\1/p' | head -1)"
fi

if [[ -z "${IDENTITY}" ]]; then
  echo "No Developer ID Application identity found." >&2
  echo "You need 'Developer ID Application' — not 'iOS Distribution'." >&2
  echo "Create at: https://developer.apple.com/account/resources/certificates/list" >&2
  echo "Then: export SIGN_IDENTITY='Developer ID Application: ...'" >&2
  exit 1
fi

echo "Using identity: ${IDENTITY}"
"${ROOT}/release/v0.2.1/resign-macos-app.sh" "${APP}" "${IDENTITY}"

if [[ ! -f "${DMG}" ]]; then
  echo "DMG not found at ${DMG}; run package-macos-arm64.sh first" >&2
  exit 1
fi

echo "==> Sign DMG"
codesign --force --timestamp --sign "${IDENTITY}" "${DMG}"
echo "DMG signature OK"

if [[ "${NOTARIZE:-0}" == "1" ]]; then
  PROFILE="${NOTARY_PROFILE:-byze-notary}"
  ZIP="/tmp/Byze-Qt-notarize.zip"
  echo "==> Notarize app"
  ditto -c -k --keepParent "${APP}" "${ZIP}"
  xcrun notarytool submit "${ZIP}" --keychain-profile "${PROFILE}" --wait
  xcrun stapler staple "${APP}"
  echo "==> Notarize DMG"
  xcrun notarytool submit "${DMG}" --keychain-profile "${PROFILE}" --wait
  xcrun stapler staple "${DMG}"
  echo "Notarization complete"
fi

(
  cd "${ROOT}/release"
  shasum -a 256 "$(basename "${DMG}")" > SHA256SUMS_Mac_arm64.txt
)

echo ""
echo "Signed release ready:"
echo "  ${APP}"
echo "  ${DMG}"
echo "  ${ROOT}/release/SHA256SUMS_Mac_arm64.txt"
