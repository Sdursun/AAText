#!/bin/sh
# Downloads the pinned Twemoji Mozilla font (COLR colour emoji) and its
# license into third_party/twemoji/, for the freetype2.lha archive.
# Font art: CC-BY 4.0 (Twitter's Twemoji); font build code: Apache 2.0
# (Mozilla). Both allow redistribution with credit.
set -e
VER=0.7.0
FONT_SHA256=6d90152ee0d29e82fe2a87793af5aa4b7ad13e6538360889e141e81ed299ee8e
LIC_SHA256=64419edc28e9163204c3be73f835a8dfc34cd6c9b8f7d067bb685f119f839a99
cd "$(dirname "$0")/.."
mkdir -p third_party/twemoji
cd third_party/twemoji
if [ ! -f Twemoji.Mozilla.ttf ]; then
    curl -sLo Twemoji.Mozilla.ttf \
        https://github.com/mozilla/twemoji-colr/releases/download/v$VER/Twemoji.Mozilla.ttf
fi
if [ ! -f LICENSE.md ]; then
    curl -sLo LICENSE.md \
        https://raw.githubusercontent.com/mozilla/twemoji-colr/v$VER/LICENSE.md
fi
echo "$FONT_SHA256  Twemoji.Mozilla.ttf" | sha256sum -c -
echo "$LIC_SHA256  LICENSE.md" | sha256sum -c -
