#!/bin/sh
# Downloads the pinned FreeType release into third_party/.
set -e
VER=2.14.3
SHA256=36bc4f1cc413335368ee656c42afca65c5a3987e8768cc28cf11ba775e785a5f
cd "$(dirname "$0")/.."
mkdir -p third_party
cd third_party
[ -d freetype-$VER ] && { echo "freetype-$VER already present"; exit 0; }
curl -sLO https://download.savannah.gnu.org/releases/freetype/freetype-$VER.tar.xz
echo "$SHA256  freetype-$VER.tar.xz" | sha256sum -c -
tar xf freetype-$VER.tar.xz
rm freetype-$VER.tar.xz
