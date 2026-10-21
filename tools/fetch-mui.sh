#!/bin/sh
# Fetches the MUI 3.8 developer files (headers for the MUI versions of
# AATextPrefs and AATextManager) into third_party/mui. Run from the
# project directory. MUI (c) Stefan Stuntz; the developer archive is on
# Aminet (dev/mui/mui38dev.lha). Only its C headers are used, at build
# time; nothing of it is part of AAText's package.
set -e
SHA=8d28b0262fa6f43a9a8e114da368a9309256bbc2f99fa1c23d8e4c105d565722
mkdir -p third_party/dl
F=third_party/dl/mui38dev.lha
[ -f $F ] || curl -sL -o $F http://aminet.net/dev/mui/mui38dev.lha
echo "$SHA  $F" | sha256sum -c -
rm -rf third_party/mui
mkdir -p third_party/mui
cd third_party/mui
lha xq ../dl/mui38dev.lha "MUI/Developer/C/Include/*" "MUI/Developer/FD/*"
# include/: libraries/mui.h and clib/ as they are; inline/ and proto/
# made with sfdc for today's gcc (the archive's inline file is the 1997
# gcc syntax)
I=MUI/Developer/C/Include
mkdir -p include/libraries include/clib include/inline include/proto
cp $I/libraries/mui.h include/libraries/
cp $I/clib/muimaster_protos.h include/clib/
fd2sfd MUI/Developer/FD/muimaster_lib.fd $I/clib/muimaster_protos.h     -o muimaster_lib.sfd
sfdc --mode=macros --target=m68k-amigaos -o include/inline/muimaster.h     muimaster_lib.sfd
sfdc --mode=proto --target=m68k-amigaos -o include/proto/muimaster.h     muimaster_lib.sfd
