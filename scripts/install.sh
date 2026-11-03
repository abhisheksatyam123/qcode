#!/usr/bin/env sh
# Installs a qcode release binary.
#
#   curl -fsSL https://raw.githubusercontent.com/abhisheksatyam123/qcode/main/scripts/install.sh | sh
#
# Environment:
#   QCODE_VERSION      release tag to install (default: latest), e.g. v0.2.0
#   QCODE_REPO         GitHub repo to download from (default: abhisheksatyam123/qcode)
#   QCODE_HOME         install root (default: $HOME/.qcode)
#   QCODE_DOWNLOAD_BASE  override the download prefix (for mirrors and testing)
#   QCODE_BIN_DIR      directory that receives the qcode symlink (default: $HOME/.local/bin)
#
# The binary and its webui/ directory go to $QCODE_HOME/<version>/, and
# $QCODE_BIN_DIR/qcode links to the binary. Downloads are checked against SHA256SUMS.
set -eu

REPO=${QCODE_REPO:-abhisheksatyam123/qcode}
HOME_DIR=${QCODE_HOME:-$HOME/.qcode}
BIN_DIR=${QCODE_BIN_DIR:-$HOME/.local/bin}

die() {
  echo "install.sh: $*" >&2
  exit 1
}

need() {
  command -v "$1" >/dev/null 2>&1 || die "required tool not found: $1"
}

need curl
need tar

case "$(uname -s)/$(uname -m)" in
  Linux/x86_64)          platform=linux-x86_64 ;;
  Darwin/arm64)          platform=macos-arm64 ;;
  *) die "no release build for $(uname -s)/$(uname -m); build from source instead" ;;
esac

version=${QCODE_VERSION:-}
if [ -z "$version" ]; then
  # /releases/latest redirects to /releases/tag/<tag>; read the tag from the redirect.
  url=$(curl -fsSLI -o /dev/null -w '%{url_effective}' "https://github.com/$REPO/releases/latest") \
    || die "could not resolve the latest release of $REPO"
  version=${url##*/}
fi
case "$version" in
  v*) ;;
  *) die "QCODE_VERSION must be a tag such as v0.2.0 (got '$version')" ;;
esac

asset="qcode-${version}-${platform}.tar.gz"
download_base=${QCODE_DOWNLOAD_BASE:-https://github.com/$REPO/releases/download}
base="$download_base/$version"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT INT TERM

echo "downloading $asset"
curl -fsSL -o "$tmp/$asset" "$base/$asset" || die "download failed: $base/$asset"
curl -fsSL -o "$tmp/SHA256SUMS" "$base/SHA256SUMS" || die "download failed: $base/SHA256SUMS"

expected=$(grep " $asset\$" "$tmp/SHA256SUMS" | cut -d ' ' -f 1) || true
[ -n "$expected" ] || die "$asset is not listed in SHA256SUMS"
if command -v sha256sum >/dev/null 2>&1; then
  actual=$(sha256sum "$tmp/$asset" | cut -d ' ' -f 1)
else
  need shasum
  actual=$(shasum -a 256 "$tmp/$asset" | cut -d ' ' -f 1)
fi
[ "$expected" = "$actual" ] || die "checksum mismatch for $asset"

dest="$HOME_DIR/$version"
rm -rf "$dest"
mkdir -p "$dest"
tar -C "$dest" -xzf "$tmp/$asset"
mkdir -p "$BIN_DIR"
ln -sf "$dest/qcode" "$BIN_DIR/qcode"

echo "installed qcode $version to $dest"
echo "linked $BIN_DIR/qcode"
case ":$PATH:" in
  *":$BIN_DIR:"*) ;;
  *) echo "note: $BIN_DIR is not on PATH; add it to your shell profile" ;;
esac
