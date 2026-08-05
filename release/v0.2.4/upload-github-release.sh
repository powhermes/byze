#!/usr/bin/env bash
# Replace macOS arm64 DMG + SHA on GitHub release v0.2.4.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
VER="0.2.4"
DMG="${ROOT}/release/byze-core-${VER}-macos-arm64.dmg"
SHA="${ROOT}/release/SHA256SUMS_Mac_arm64.txt"
RELEASE_ID="344826669"
OLD_DMG_ASSET="457785974"
OLD_SHA_ASSET="457786091"
REPO="powhermes/byze"

TOKEN="$(printf 'protocol=https\nhost=github.com\n\n' | git credential fill | awk '/^password=/ {sub(/^password=/,""); print}')"
if [[ -z "${TOKEN}" ]]; then
  echo "No GitHub token in git credential" >&2
  exit 1
fi

for f in "${DMG}" "${SHA}"; do
  [[ -f "${f}" ]] || { echo "Missing ${f}" >&2; exit 1; }
done

echo "==> Verify notarization"
spctl -a -vv -t install "${DMG}" 2>&1 | grep -q "accepted" || {
  echo "DMG not accepted by Gatekeeper yet — staple/notarize first" >&2
  exit 1
}
xcrun stapler validate "${DMG}" >/dev/null

echo "==> Delete old release assets"
for aid in "${OLD_DMG_ASSET}" "${OLD_SHA_ASSET}"; do
  curl -sS -X DELETE \
    -H "Authorization: Bearer ${TOKEN}" \
    -H "Accept: application/vnd.github+json" \
    "https://api.github.com/repos/${REPO}/releases/assets/${aid}" >/dev/null
  echo "  deleted asset ${aid}"
done

upload() {
  local file="$1"
  local name="$2"
  local ctype="$3"
  echo "==> Upload ${name}"
  curl -sS -X POST \
    -H "Authorization: Bearer ${TOKEN}" \
    -H "Content-Type: ${ctype}" \
    --data-binary @"${file}" \
    "https://uploads.github.com/repos/${REPO}/releases/${RELEASE_ID}/assets?name=${name}" \
    | python3 -c "import sys,json; a=json.load(sys.stdin); print(f\"  uploaded {a.get('name')} sha256={a.get('digest','?')}\")"
}

upload "${DMG}" "byze-core-${VER}-macos-arm64.dmg" "application/octet-stream"
upload "${SHA}" "SHA256SUMS_Mac_arm64.txt" "text/plain"

echo ""
echo "Release updated: https://github.com/${REPO}/releases/tag/v${VER}"
cat "${SHA}"