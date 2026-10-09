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
run auto            tests/data/empty.prefs auto AutoTest.font
run charsets        tests/charset.prefs charsets

# .otag written on a volume of another name: "Gone:Fonts/..." must be
# found again in FONTS:
M=$(mktemp -d)
mkdir -p $M/moved_fonts_test_directory
cp tests/data/MovedTest.otag $M/
cp third_party/dejavu-fonts-ttf-2.37/ttf/DejaVuSans.ttf $M/moved_fonts_test_directory/
V="vamos -C 68020 -m 8192 -V movedfonts:$M -a FONTS:movedfonts:"
run moved-volume    tests/data/empty.prefs auto MovedTest.font
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
V="vamos -C 68020 -m 8192 -V scanfonts:$M -a FONTS:scanfonts:"
run scan            tests/data/empty.prefs scan Good=ok Moved=moved \
                    Missing=missing NotFont=badfile Broken=badotag
rm -rf $M

echo
if [ $fail -eq 0 ]; then
    echo "all tests passed"
else
    echo "$fail test(s) failed"
fi
exit $fail
