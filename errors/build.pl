#!/usr/bin/perl -w

use strict;
use IO::File;

sub cnv($$$) {
	my ($fn, $dfn, $cm) = @_;

	my ($fh) = new IO::File;
	my ($dfh) = new IO::File;
	$fh->open($fn) || die "Couldn't open $fn: $!\n";
	$dfh->open($dfn, "w") || die "Couldn't open $fn: $!\n";
	while (<$fh>) {
		# Do the inline substitution
		s/@(.*?)@/$cm->{$1}/ge;
		print $dfh $_;
	}
	$fh->close;
	$dfh->close;
}

my ($srcdir) = shift @ARGV || die "missing source directory!\n";
my ($dstdir) = shift @ARGV || die "missing destination directory!\n";

print "Converting $srcdir/ERR_* to $dstdir/..\n";

# Suck in the CSS into a local array

my ($fh) = new IO::File;
$fh->open("error.css") || die "Couldn't open error.css: $!\n";
my ($css) = "";
while (<$fh>) { $css .= $_; }
$fh->close;
undef $fh;

my (%cm);
$cm{"STYLE"} = '<STYLE type="text/css"><!--' . $css . "--></STYLE>\n";

# Get the list of files to create
my ($dh);
opendir $dh, $srcdir || die "Couldn't opendir src/: $!\n";
my (@dents) = grep { /^ERR_/ && -f $srcdir . "/$_" } readdir($dh);
closedir $dh;

# Now, loop over our source array and convert stuff
foreach (@dents) {
	print "Converting: $_\n";
	cnv($srcdir . "/" . $_, $dstdir . "/" . $_, \%cm);
}

