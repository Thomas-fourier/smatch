#!/usr/bin/perl
use strict;
use warnings;
use Data::Dumper;


sub uniq (@) {
    my %seen = ();
    grep { not $seen{$_}++ } @_;
}

my %derefed = ();
my %tested = ();

while(<>) {
    if(/deref of \((\w+\.\w+.\d+)\) \((\w+)\)/) {
        push(@{$derefed{"$1"}}, $2);
    } elsif(/test of\((\w+\.\w+.\d+)\) \((\w+)\)/) {
        push(@{$tested{"$1"}}, $2);
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
    for my $impl (uniq @{$derefed{$arg}}) {
        $nb_dref++;
        if (grep(/^$impl$/, @{$tested{$arg}})) {
            $nb_test++;
        } else {
            push(@sus, $impl)
        }
    }

    my $prop = $nb_test/$nb_dref;

    next if ($prop <= 0.5 or $prop == 1);

    print "$arg tested $prop before deref\n";

    foreach my $i ( 1 .. $#sus) {
        print "\tNot tested in: " . $sus[$i] . "\n";
    }


}


