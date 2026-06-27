#!/usr/bin/env bash
# Re-sign every Mach-O inside a macOS .app bundle (inside-out).
# Usage: resign-macos-app.sh <path/to/Byze-Qt.app> [identity]
# Default identity is ad-hoc (-).

set -euo pipefail

APP="${1:?app bundle path required}"
IDENTITY="${2:--}"
ENTITLEMENTS="${ENTITLEMENTS:-}"

sign_one() {
  local target="$1"
  if [[ -n "${ENTITLEMENTS}" ]]; then
    codesign --force --options runtime --entitlements "${ENTITLEMENTS}" --sign "${IDENTITY}" "${target}"
  elif [[ "${IDENTITY}" == "-" ]]; then
    codesign --force --sign "${IDENTITY}" "${target}"
  else
    codesign --force --options runtime --timestamp --sign "${IDENTITY}" "${target}"
  fi
}

is_macho() {
  file "$1" 2>/dev/null | grep -q 'Mach-O'
}

echo "==> Signing ${APP} with identity: ${IDENTITY}"

while IFS= read -r -d '' f; do
  if is_macho "$f"; then
    sign_one "$f"
  fi
done < <(find "${APP}/Contents/Frameworks" "${APP}/Contents/PlugIns" -type f \
  ! -path '*/_CodeSignature/*' ! -name '*.plist' ! -name '*.prl' ! -name '*.xcprivacy' -print0 2>/dev/null || true)

sign_one "${APP}/Contents/MacOS/Byze-Qt"
sign_one "${APP}"

codesign --verify --deep --strict --verbose=2 "${APP}"
echo "==> Signature OK"
