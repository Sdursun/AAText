#!/bin/sh
# Downloads the pinned HarfBuzz release into third_party/ (harfbuzz.library,
# see HARFBUZZ_PLAN.md). The checksum is the one GitHub lists for the
# release asset.
set -e
VER=15.1.0
SHA256=0fc7c565443a5f4be7da65ac5ee5bd737d9b158dc908708b7689a0a5796b0c8d
cd "$(dirname "$0")/.."
mkdir -p third_party
cd third_party
[ -d harfbuzz-$VER ] && { echo "harfbuzz-$VER already present"; exit 0; }
curl -sLO https://github.com/harfbuzz/harfbuzz/releases/download/$VER/harfbuzz-$VER.tar.xz
echo "$SHA256  harfbuzz-$VER.tar.xz" | sha256sum -c -
xz -dc harfbuzz-$VER.tar.xz | tar xf -
rm harfbuzz-$VER.tar.xz
