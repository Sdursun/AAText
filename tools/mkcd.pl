#!/usr/bin/perl
# Writes the catalog description (.cd) and an empty translation (.ct)
# for translators, in the format FlexCat and CatComp read, from the
# S(NAME, id, "English") entries of a strings.h.
#
#   perl tools/mkcd.pl <strings.h> <name> <version> <outdir>
#
# <name> is the catalog's name without ".catalog" (aatextprefs);
# <outdir>/<name>.cd and <outdir>/<name>.ct are written. Text is
# plain ASCII; translators save the .ct in their language's Amiga
# character set.
use strict;
use warnings;

my ($hfile, $name, $version, $outdir) = @ARGV;
die "usage: mkcd.pl strings.h name version outdir\n" unless $outdir;

my @s;
open(my $h, '<', $hfile) or die "$hfile: $!";
while (<$h>) {
    push @s, [$1, $2, $3] if /S\((\w+),\s*(\d+),\s*"((?:[^"\\]|\\.)*)"\)/;
}
close $h;
die "$hfile: no strings\n" unless @s;
for (@s) {
    die "$hfile: $_->[0]: only \\n and \\\" escapes are allowed\n"
        if $_->[2] =~ /\\[^n"]/;
}

my @lt = localtime;
my $date = sprintf "%d.%d.%d", $lt[3], $lt[4] + 1, $lt[5] + 1900;

open(my $cd, '>', "$outdir/$name.cd") or die "$outdir/$name.cd: $!";
print $cd "; $name.cd - the English texts of $name.catalog\n";
print $cd "; (FlexCat / CatComp catalog description; do not change)\n;\n";
for (@s) {
    print $cd "$_->[0] ($_->[1]//)\n$_->[2]\n;\n";
}
close $cd;

open(my $ct, '>', "$outdir/$name.ct") or die "$outdir/$name.ct: $!";
print $ct "## version \$VER: $name.catalog $version ($date)\n";
print $ct "## language <your language, e.g. deutsch>\n";
print $ct "## codeset 0\n;\n";
print $ct "; Write the translation of each text on the empty line below its\n";
print $ct "; name; the English text follows as a comment. Keep %s, %ld and\n";
print $ct "; \\n as they are, and the order of the %-codes. A \"_\" marks the\n";
print $ct "; key of a button. Save in your language's Amiga character set.\n;\n";
for (@s) {
    print $ct "$_->[0]\n\n; $_->[2]\n;\n";
}
close $ct;
printf "%s: %d strings\n", $name, scalar @s;
