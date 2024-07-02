#!/usr/bin/perl -w

# This evil hacky script takes in a file with one hex number
# per line and outputs a lookup map.

# Adrian Chadd <adrian@creative.net.au>

use strict;

my (@ar);

for (my $i = 0; $i < 256; $i++) {
	if ($i < 32 || $i > 127) {
		$ar[$i] = 1;
	} else {
		$ar[$i] = 0;
	}
}

while (<>) {
	chomp;
	$ar[hex($_)] = 1;
};

for (my $i = 0; $i < 256; $i++) {
	printf "%s, ", $ar[$i];
	if ($i > 0 && ($i % 16 == 15)) { print "\n"; }
}
print "\n";
