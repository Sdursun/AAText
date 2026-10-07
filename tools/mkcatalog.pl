#!/usr/bin/perl
# Builds an AmigaOS locale catalog (IFF FORM CTLG) from a translation.
#
#   perl tools/mkcatalog.pl <strings.h> <language.ct> <out.catalog>
#
# strings.h lists S(NAME, id, "English") entries; the .ct file (UTF-8)
# has "NAME" lines each followed by its translation, plus
# "## language <name>" and "## version <$VER string>". Text is written
# as ISO-8859-9 (Latin-5), the Amiga's Turkish character set.
#
# STRS entries: LONG id, LONG length (incl. NUL), string, padded to a
# multiple of 4 - the layout locale.library reads.
use strict;
use warnings;
use Encode qw(decode encode);

my ($hfile, $ctfile, $out) = @ARGV;
die "usage: mkcatalog.pl strings.h language.ct out.catalog\n" unless $out;

my %id;
open(my $h, '<', $hfile) or die "$hfile: $!";
while (<$h>) {
    $id{$1} = $2 if /S\((\w+),\s*(\d+),/;
}
close $h;

my ($language, $version) = ('', '');
my %text;
open(my $ct, '<:raw', $ctfile) or die "$ctfile: $!";
my @lines = map { s/\r?\n$//r } <$ct>;
close $ct;
for (my $i = 0; $i < @lines; $i++) {
    my $l = decode('UTF-8', $lines[$i]);
    if ($l =~ /^## language (.+)$/) { $language = $1; next; }
    if ($l =~ /^## version (.+)$/)  { $version = $1;  next; }
    next if $l =~ /^;/ || $l =~ /^\s*$/;
    die "$ctfile: unknown string $l\n" unless exists $id{$l};
    my $t = decode('UTF-8', $lines[++$i] // '');
    $t =~ s/\\n/\n/g;
    $text{$id{$l}} = $t;
}
my @missing = grep { !exists $text{$id{$_}} } sort keys %id;
warn "warning: untranslated: @missing\n" if @missing;

sub latin5 { encode('iso-8859-9', $_[0], Encode::FB_CROAK) }

sub chunk {
    my ($tag, $data) = @_;
    my $c = $tag . pack('N', length $data) . $data;
    $c .= "\0" if length($data) & 1;
    return $c;
}

my $strs = '';
for my $n (sort { $a <=> $b } keys %text) {
    my $s = latin5($text{$n}) . "\0";
    my $e = pack('NN', $n, length $s) . $s;
    $e .= "\0" while length($e) % 4;
    $strs .= $e;
}

my $body = 'CTLG'
         . chunk('FVER', latin5($version) . "\0")
         . chunk('LANG', latin5($language) . "\0")
         . chunk('STRS', $strs);
open(my $o, '>:raw', $out) or die "$out: $!";
print $o 'FORM' . pack('N', length $body) . $body;
close $o;
printf "%s: %d strings, language %s\n", $out, scalar(keys %text), $language;
