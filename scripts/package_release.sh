#!/usr/bin/env bash
# Packages a built qcode binary as a release tarball.
#
# Usage: scripts/package_release.sh <build-apps-qcode-dir> <platform> <out-dir>
#   e.g. scripts/package_release.sh build/release/apps/qcode linux-x86_64 dist
#
# The tarball holds qcode/qcode, qcode/webui/ (the server resolves webui next
# to the binary) and README.md. The version comes from GITHUB_REF_NAME when set
# (tag builds), otherwise from `git describe`.
set -euo pipefail

if [[ $# -ne 3 ]]; then
  echo "usage: $0 <build-apps-qcode-dir> <platform> <out-dir>" >&2
  exit 2
fi

src_dir=$1
platform=$2
out_dir=$3
repo_root=$(cd "$(dirname "$0")/.." && pwd)

binary="$src_dir/qcode"
webui="$src_dir/webui"
if [[ ! -x "$binary" ]]; then
  echo "error: binary not found: $binary" >&2
  exit 1
fi
if [[ ! -d "$webui" ]]; then
  echo "error: webui not found next to the binary: $webui" >&2
  exit 1
fi

version=${GITHUB_REF_NAME:-}
if [[ -z "$version" ]]; then
  version=$(git -C "$repo_root" describe --tags --always --dirty 2>/dev/null || echo dev)
fi

name="qcode-${version}-${platform}"
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT

mkdir -p "$stage/qcode"
cp "$binary" "$stage/qcode/qcode"
cp -R "$webui" "$stage/qcode/webui"
cp "$repo_root/README.md" "$stage/qcode/README.md"

mkdir -p "$out_dir"
tar -C "$stage/qcode" -czf "$out_dir/$name.tar.gz" .
echo "wrote $out_dir/$name.tar.gz"
