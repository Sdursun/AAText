#!/bin/sh
# Runs every fttest mode under vamos with the settings file it needs and
# checks each return code. Run inside the aatext-vamos container after
# "make test" (test.ps1 -All does both).
#
# Needs third_party/dejavu-fonts-ttf-2.37 (tools/fetch-testfonts.sh).

V="vamos -C 68020 -m 8192 -V testfonts:$(pwd)/tests/data/fonts -a FONTS:testfonts:"
fail=0

run()
{
    name=$1
    shift
    out=$($V build/68020/fttest "$@" 2>&1)
    rc=$?
    if [ $rc -eq 0 ]; then
        echo "ok    $name"
    else
        echo "FAIL  $name (return code $rc)"
        echo "$out" | tail -15 | sed 's/^/      /'
        fail=$((fail + 1))
    fi
}

run render          tests/test.prefs Ag
run stress          tests/test.prefs stress
run baseline        tests/test.prefs baseline
run capsize         tests/capsize.prefs capsize
run metrics         tests/real.prefs metrics
run metrics-nokern  tests/realnokern.prefs metrics
run otag            tests/test.prefs otag tests/data/arial.otag
run otagrw-ftm      tests/data/empty.prefs otagrw tests/data/verdanaregular.otag
run otagrw-cff      tests/data/empty.prefs otagrw tests/data/sourcesans3regular.otag
run otagrw-ttf      tests/data/empty.prefs otagrw tests/data/arial.otag
run auto            tests/data/empty.prefs auto AutoTest.font
run charsets        tests/charset.prefs charsets
run fontinfo        tests/data/empty.prefs fontinfo third_party/dejavu-fonts-ttf-2.37/ttf/DejaVuSans.ttf 0 dejavusansbook
run fontinfo-bi     tests/data/empty.prefs fontinfo third_party/dejavu-fonts-ttf-2.37/ttf/DejaVuSansMono-BoldOblique.ttf 0 dejavusansmonoboldoblique

# .otag written on a volume of another name: "Gone:Fonts/..." must be
# found again in FONTS:
M=$(mktemp -d)
mkdir -p $M/moved_fonts_test_directory
cp tests/data/MovedTest.otag $M/
cp third_party/dejavu-fonts-ttf-2.37/ttf/DejaVuSans.ttf $M/moved_fonts_test_directory/
V="vamos -C 68020 -m 8192 -V movedfonts:$M -a FONTS:movedfonts:"
run moved-volume    tests/data/empty.prefs auto MovedTest.font
rm -rf $M

# font installer: into an empty directory, compared with FTManager's
# .otag for Verdana
M=$(mktemp -d)
V="vamos -C 68020 -m 8192 -V inst:$M"
run install         tests/data/empty.prefs install third_party/dejavu-fonts-ttf-2.37/ttf/DejaVuSans.ttf inst: tests/data/verdanaregular.otag
rm -rf $M

# font diagnostics: one .otag for each status
M=$(mktemp -d)
mkdir -p $M/moved_fonts_test_directory
cp third_party/dejavu-fonts-ttf-2.37/ttf/DejaVuSans.ttf $M/moved_fonts_test_directory/
echo "not a font" > $M/moved_fonts_test_directory/NotAFont__.ttf
cp tests/data/fonts/AutoTest.otag $M/Good.otag
cp tests/data/MovedTest.otag $M/Moved.otag
perl -pe 's/DejaVuSans\.ttf/NotThere__.ttf/' tests/data/MovedTest.otag > $M/Missing.otag
perl -pe 's/DejaVuSans\.ttf/NotAFont__.ttf/' tests/data/MovedTest.otag > $M/NotFont.otag
echo "garbage" > $M/Broken.otag
perl tests/otag-engine.pl tests/data/fonts/AutoTest.otag aatext > $M/AAEngine.otag
# FTManager .otag: engine freetype2, no code page, "Fonts:_ttf/verdana.ttf"
cp tests/data/verdanaregular.otag $M/FT2.otag
mkdir $M/_ttf
cp third_party/dejavu-fonts-ttf-2.37/ttf/DejaVuSans.ttf $M/_ttf/verdana.ttf
# an engine library for the engine column: aatext.library 1.0, no ttf.library
mkdir $M/libs
printf '$VER: aatext.library 1.0 (09/10/26)\0' > $M/libs/aatext.library
V="vamos -C 68020 -m 8192 -V scanfonts:$M -a FONTS:scanfonts: -V scanlibs:$M/libs -a LIBS:scanlibs:"
run scan            tests/data/empty.prefs scan Good=ok Moved=moved \
                    Missing=missing NotFont=badfile Broken=badotag \
                    AAEngine=ok "AAEngine@aatext 1.0" "Good@ttf ?"
run fix             tests/data/empty.prefs fix Moved
run repair          tests/data/empty.prefs repair FT2
rm -rf $M

# library versions from the $VER string in LIBS: (placed across the 4 KB
# read boundary), and without the library
M=$(mktemp -d)
mkdir $M/libs $M/nolibs
{ head -c 4090 /dev/zero; printf '$VER: aatext.library 1.0 (09/10/26)\0'; head -c 100 /dev/zero; } > $M/libs/aatext.library
printf '$VER: freetype2.library 1.3 (01.01.2002)\0' > $M/libs/freetype2.library
V="vamos -C 68020 -m 8192 -V ft2:$M/libs -a LIBS:ft2:"
run libver-aatext   tests/data/empty.prefs libver aatext.library 1.0
run libver-ft2      tests/data/empty.prefs libver freetype2.library 1.3
V="vamos -C 68020 -m 8192 -V ft2:$M/nolibs -a LIBS:ft2:"
run libver-none     tests/data/empty.prefs libver aatext.library none
rm -rf $M

echo
if [ $fail -eq 0 ]; then
    echo "all tests passed"
else
    echo "$fail test(s) failed"
fi
exit $fail
