#!/bin/sh
# Downloads DejaVu fonts into third_party/ for tests/test.prefs.
set -e
cd "$(dirname "$0")/.."
mkdir -p third_party
cd third_party
[ -d dejavu-fonts-ttf-2.37 ] && { echo "DejaVu already present"; exit 0; }
curl -sL -o dejavu.tar.bz2 https://github.com/dejavu-fonts/dejavu-fonts/releases/download/version_2_37/dejavu-fonts-ttf-2.37.tar.bz2
tar xjf dejavu.tar.bz2
rm dejavu.tar.bz2
