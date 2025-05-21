#!/usr/bin/perl
use strict;
use warnings;
use Data::Dumper;

my %derefed = ();
my %tested = ();
my %all = ();


while(<>) {
    if(/^deref of \((\w+\.\w+.\d+)\) \((\w+.\w+)\) ([\w.\/]+:\d+)$/) {
        # We are interested in the first deref
        if (! exists($derefed{$1}{$2})) {$derefed{$1}{$2} = $3};
        if (! exists($all{$1}{$2})) {$all{$1}{$2} = ()};
    } elsif(/^test of \((\w+\.\w+.\d+)\) \((\w+.\w+)\) ([\w.\/]+:\d+)$/) {
        if (! exists($tested{$1}{$2})) {$tested{$1}{$2} = $3};
        if (! exists($all{$1}{$2})) {$all{$1}{$2} = ()};
    }
}


for my $arg (sort keys %all) {
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
            print "Warning: $arg deref and test in same impl, $tested{$arg}{$impl}\n";
            $nb_dref--;
        } else {
            push(@sus, $impl . " " . $derefed{$arg}{$impl})
        }
    }

    for my $impl (keys %{$tested{$arg}}) {
        $nb_test++;
    }

    my $prop = $nb_test/($nb_dref+$nb_test);

    next if ($prop <= 0.5 or $prop == 1);

    print "$arg tested $prop before deref\n";

    foreach my $i ( 1 .. $#sus) {
        print "\tNot tested in: " . $sus[$i] . "\n";
    }


}


