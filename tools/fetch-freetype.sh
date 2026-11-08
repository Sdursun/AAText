#!/bin/sh
# Downloads the pinned FreeType release into third_party/.
set -e
VER=2.15.0
SHA256=93cd47eb719c325107f2d3cf087d06299e944c3b48a40b55537302304dea1dd5
cd "$(dirname "$0")/.."
mkdir -p third_party
cd third_party
[ -d freetype-$VER ] && { echo "freetype-$VER already present"; exit 0; }
curl -sLO https://download.savannah.gnu.org/releases/freetype/freetype-$VER.tar.xz
echo "$SHA256  freetype-$VER.tar.xz" | sha256sum -c -
tar xf freetype-$VER.tar.xz
rm freetype-$VER.tar.xz
