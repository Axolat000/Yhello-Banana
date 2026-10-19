#!/usr/bin/perl
# SPDX-License-Identifier: GPL-3.0-or-later
# embed.pl in.js NAME > out.h : a script file as a C string constant
use strict;
my ($in, $name) = @ARGV;
open(my $f, '<', $in) or die "$in: $!";
print "/* generated from $in by web/test/embed.pl: do not edit */\n";
print "static const char ${name}[] =\n";
while (my $l = <$f>) {
    chomp $l;
    $l =~ s/\r$//;
    $l =~ s/\\/\\\\/g;
    $l =~ s/"/\\"/g;
    print "    \"$l\\n\"\n";
}
print ";\n";
