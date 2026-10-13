#!/bin/sh
# Fetches the AmiSSL 5 SDK (headers for the update check of
# AATextManager) into third_party/amissl-sdk. Run from the project
# directory. AmiSSL: https://github.com/jens-maus/amissl (Apache 2.0).
set -e
VER=5.27
SHA=5003bef8c5930354d16b0ce7196d71b2811891c42fad38a9238c5ce4098ad42a
mkdir -p third_party/dl
F=third_party/dl/AmiSSL-$VER-SDK.lha
[ -f $F ] || curl -sL -o $F \
    https://github.com/jens-maus/amissl/releases/download/$VER/AmiSSL-$VER-SDK.lha
echo "$SHA  $F" | sha256sum -c -
rm -rf third_party/amissl-sdk
mkdir -p third_party/amissl-sdk
cd third_party/amissl-sdk && lha xq ../dl/AmiSSL-$VER-SDK.lha
