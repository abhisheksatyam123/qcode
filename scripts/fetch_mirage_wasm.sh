#!/usr/bin/env bash
# Fetch the in-browser Mirage bundle (Cactus WASM engine + Mirage weights) for the Spiddy
# "look" feature. The files are NOT committed to the repo (~21 MB). Every file is pinned by
# size and sha256; a download that does not match is discarded.
#
#   scripts/fetch_mirage_wasm.sh [--check] [DEST]
#
#   DEST     directory to fill (default: $QCODE_MIRAGE_WASM_DIR or <repo>/build/mirage-wasm)
#   --check  verify what is already in DEST, download nothing
#
# Sources:
#   cactus.js, cactus.wasm  Cactus-Compute/cactus @ 4e822c90 (release 4.1.0, wasm/ entries
#                           of releases/4.1.0/manifest.json)
#   mirage.cact             Cactus-Compute/mirage @ 76c1e9e5
# Serve DEST at a URL and set it with qcodeSpiddy.setMirageWasmBase(url). See docs/mascot.md.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CHECK_ONLY=0
DEST=""
for arg in "$@"; do
  case "$arg" in
    --check) CHECK_ONLY=1 ;;
    -h|--help) sed -n '2,15p' "$0"; exit 0 ;;
    *) DEST="$arg" ;;
  esac
done
DEST="${DEST:-${QCODE_MIRAGE_WASM_DIR:-$REPO/build/mirage-wasm}}"

HF=https://huggingface.co
CACTUS_REV=4e822c9036be2eb0e47ef556df32a818bb1572ef
MIRAGE_REV=76c1e9e53381aca8fc3843c636216c96ff220122

# name | url | size | sha256
FILES=(
  "cactus.js|$HF/Cactus-Compute/cactus/resolve/$CACTUS_REV/releases/4.1.0/wasm/cactus.js|62247|707341e44776ab0eb8dfc8f6a579ed5ac41e2ffc6419a18012f61619a6dfc70c"
  "cactus.wasm|$HF/Cactus-Compute/cactus/resolve/$CACTUS_REV/releases/4.1.0/wasm/cactus.wasm|1241811|6fb6cb41145cd51fd8af97644cd684fbdd434d4d3ce261881df58cbaef5b67f7"
  "mirage.cact|$HF/Cactus-Compute/mirage/resolve/$MIRAGE_REV/mirage.cact|21611043|4249a0f7515272ff76b9414bec17d7e7daf84b15b66c7fb4f17fce1e66f9f237"
)

sha256_of() {
  if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | cut -d' ' -f1
  else shasum -a 256 "$1" | cut -d' ' -f1; fi
}

# A file is good when its size and sha256 both match the pinned values.
file_ok() { # path size sha
  [[ -f "$1" ]] || return 1
  [[ "$(wc -c < "$1" | tr -d ' ')" == "$2" ]] || return 1
  [[ "$(sha256_of "$1")" == "$3" ]]
}

[[ $CHECK_ONLY -eq 1 ]] || mkdir -p "$DEST"
status=0
for entry in "${FILES[@]}"; do
  IFS='|' read -r name url size sha <<< "$entry"
  target="$DEST/$name"
  if file_ok "$target" "$size" "$sha"; then
    echo "ok       $name"
    continue
  fi
  if [[ $CHECK_ONLY -eq 1 ]]; then
    echo "MISSING  $name (or mismatch)"; status=1; continue
  fi
  tmp="$(mktemp "$DEST/.$name.XXXXXX")"
  echo "fetch    $name ($size bytes)"
  if ! curl -fsSL --retry 3 --retry-delay 2 --connect-timeout 20 -o "$tmp" "$url"; then
    rm -f "$tmp"; echo "FAILED   $name: download error" >&2; status=1; continue
  fi
  if ! file_ok "$tmp" "$size" "$sha"; then
    got_size="$(wc -c < "$tmp" | tr -d ' ')"
    rm -f "$tmp"
    echo "FAILED   $name: size $got_size (want $size) or sha256 mismatch" >&2
    status=1; continue
  fi
  mv -f "$tmp" "$target"
  echo "ok       $name"
done

if [[ $status -eq 0 ]]; then
  echo "Mirage WASM bundle ready in: $DEST"
else
  echo "Mirage WASM bundle incomplete in: $DEST" >&2
fi
exit $status
