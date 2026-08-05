#!/usr/bin/env bash
# Finish notarization: poll Apple, staple app + DMG when accepted.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
VER="0.2.4"
APP="${ROOT}/release/v${VER}/macos-arm64/Byze-Qt.app"
DMG="${ROOT}/release/byze-core-${VER}-macos-arm64.dmg"
PROFILE="${NOTARY_PROFILE:-byze-notary}"
SUBMISSION_ID="${1:-56eedbe7-58fd-440c-82e5-635821922bc6}"

echo "Polling submission ${SUBMISSION_ID}..."
for i in $(seq 1 480); do
  STATUS="$(xcrun notarytool info "${SUBMISSION_ID}" --keychain-profile "${PROFILE}" 2>&1 | awk -F': ' '/^  status:/ {print $2}')"
  echo "  [${i}] status: ${STATUS:-unknown}"
  if [[ "${STATUS}" == "Accepted" ]]; then
    echo "==> Staple app"
    xcrun stapler staple "${APP}"
    echo "==> Staple DMG"
    xcrun stapler staple "${DMG}"
    (
      cd "${ROOT}/release"
      shasum -a 256 "$(basename "${DMG}")" > SHA256SUMS_Mac_arm64.txt
    )
    cp "${DMG}" ~/Desktop/byze-core-${VER}-macos-arm64-notarized.dmg
    cp "${ROOT}/release/SHA256SUMS_Mac_arm64.txt" ~/Desktop/
    echo ""
    spctl -a -vv -t install "${DMG}" 2>&1 || true
    xcrun stapler validate "${DMG}" 2>&1 || true
    echo ""
    echo "Done. Notarized DMG:"
    echo "  ${DMG}"
    echo "  ~/Desktop/byze-core-${VER}-macos-arm64-notarized.dmg"
    echo "==> Update GitHub release v${VER}"
    "$(dirname "$0")/upload-github-release.sh"
    exit 0
  fi
  if [[ "${STATUS}" == "Invalid" ]]; then
    echo "Notarization rejected. Log:"
    xcrun notarytool log "${SUBMISSION_ID}" --keychain-profile "${PROFILE}"
    exit 1
  fi
  sleep 30
done
echo "Timed out waiting for Apple notarization." >&2
exit 1