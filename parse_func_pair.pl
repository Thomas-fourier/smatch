#!/usr/bin/perl

use Text::Levenshtein qw(distance);

my %score = ();
my %func_def = ();

sub avg {
    my $res = 0;
    foreach my $i (@_) {
        $res += $i;
    }
    return $res / @_
}

while(<>) {
    # funct pair: dma_map_sg_attrs crypto_tfm_clear_flags 1.869792
    if(/^funct pair: (\w+) (\w+) (\d+\.?\d+)$/) {
        if ($1 < $2) { # keep the functions in alphabetical order
            push(@{$score{$1}{$2}}, $3)
        } else {
            push(@{$score{$2}{$1}}, $3)
        }
    } elsif (/^Defining ([\w_\d]+) in ([\w_\/\.]+)$/) {
        $func_def{$1} = $2
    }
}

my @res;

for my $fun1 (keys %score) {
    #print $fun1 . "\n";
    for my $fun2 (keys %{$score{$fun1}}) {
        if ($func_def{$fun1} == $func_def{$fun2}) {
            push(@res, scalar(@{$score{$fun1}{$fun2}}) # sqrt(avg(@{$score{$fun1}{$fun2}})) / distance($fun1, $fun2)
                 . " " . $fun1 . " " . $fun2);
        }
    }
}

for my $line (sort { $a <=> $b } @res) {
    print $line . "\n";
}
