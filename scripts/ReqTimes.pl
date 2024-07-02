#!/usr/bin/perl -w


use strict;

# This is a simple script that summaries hit requests over a given
# interval (second, minute, hour, etc) and provides a list with the number
# of requests.
#
# It should be extended to provide a breakdown of reply types.
#
# The output is unsorted - sorting is the job of another tool.
#
# The tool is also very naive and is intended for finding "hotspots" in the
# access.log file; it isn't inteded to provide per-day type statistics.
#
# Adrian Chadd <adrian@squid-cache.org>
# $Id: PerUser.pl 11572 2007-01-24 08:03:52Z adrian $

use Squid::ParseLog;

# For now, its hardcoded per-minute; this is easy to change.

my ($time_divisor) = 60;

sub aggregate_time($$) {
	my ($t, $d) = @_;
	return int(($t / $d)) * $d;
}

my %u;
my $wh;

if (scalar @ARGV < 1) {
	print "ERROR: First argument should be the time divisor (in seconds)\n";
	exit 1;
}
$time_divisor = $ARGV[0];
shift @ARGV;

while (<>) {
	chomp;
	my $l = Squid::ParseLog::parse($_);
	my ($lk) = aggregate_time($l->{"timestamp"}, $time_divisor);
	if (! defined $u{$lk}) {
		$u{$lk}->{"traffic"} = 0;
		$u{$lk}->{"reqcount"} = 0;
	}
	$u{$lk}->{"traffic"} += $l->{"size"};
	$u{$lk}->{"reqcount"} += 1;
}

foreach (keys %u) {
	printf "%s,%lu\n", $_, $u{$_}->{"reqcount"};
}
