#!/usr/bin/perl
use strict;
use warnings;
use Data::Dumper;

my %derefed = ();
my %tested = ();



while(<>) {
    if(/^deref of \((\w+\.\w+.\d+)\) \((\w+.\w+)\) ([\w.\/]+:\d+)$/) {
        # We are interested in the first deref
        next if exists($derefed{$1}{$2});
        $derefed{$1}{$2} = $3;
    } elsif(/^test of\((\w+\.\w+.\d+)\) \((\w+.\w+)\) ([\w.\/]+:\d+)$/) {
        next if exists($tested{$1}{$2});
        $tested{$1}{$2} = $3;
    }
}


for my $arg (sort keys %derefed) {
    #print $arg;
    # If never tested, then it is consistent
    next if (not defined $tested{$arg});

    # Otherwise, we need to count the unique pointers
    # dereferenced and how often they are tested
    my $nb_dref = 0;
    my $nb_test = 0;
    my @sus = [];
    for my $impl (keys %{$derefed{$arg}}) {
        $nb_dref++;
        if (exists($tested{$arg}{$impl})) {
            $nb_test++;
        } else {
            push(@sus, $impl . " " . $derefed{$arg}{$impl})
        }
    }

    my $prop = $nb_test/$nb_dref;

    next if ($prop <= 0.5 or $prop == 1);

    print "$arg tested $prop before deref\n";

    foreach my $i ( 1 .. $#sus) {
        print "\tNot tested in: " . $sus[$i] . "\n";
    }


}


