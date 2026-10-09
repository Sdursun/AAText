#!/usr/bin/perl
# Writes a copy of a .otag with another OT_Engine name, for the tests:
#
#   perl tests/otag-engine.pl <in.otag> <engine> > out.otag
#
# The new name is appended (4-byte aligned), the OT_Engine tag points to
# it and OT_FileIdent (the file size) is updated, as aa_OTagSetFontFile()
# does for the font file.
use strict;
use warnings;

my ($in, $engine) = @ARGV;
die "usage: otag-engine.pl in.otag engine\n" unless defined $engine;
open(my $f, '<:raw', $in) or die "$in: $!";
my $d = do { local $/; <$f> };
close $f;

my $off = (length($d) + 3) & ~3;
$d .= "\0" x ($off - length $d);
$d .= "$engine\0";
for (my $p = 0; $p + 8 <= $off; $p += 8) {
    my $tag = unpack('N', substr($d, $p, 4));
    last if $tag == 0;
    substr($d, $p + 4, 4) = pack('N', $off) if $tag == 0x80001002;
}
substr($d, 4, 4) = pack('N', length $d);
binmode STDOUT;
print $d;
