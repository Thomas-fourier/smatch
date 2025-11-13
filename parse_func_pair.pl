#!/usr/bin/perl


use File::Basename;
use File::Map qw(map_file);

my $filename = $ARGV[0];
my %score = ();
my %func_def = ();

sub avg {
    my $res = 0;
    foreach my $i (@_) {
        $res += $i;
    }
    return $res / @_
}

sub is_in_doc {
    my ($fun1, $fun2) = @_;
    # path of header (suppose logs is in linux folder)
    if ($func_def{$fun1} eq "") {
        print("No definition of the function $fun1 found\n");
        return 0;
    }
    my $file_fun1 = dirname($filename) . "/" . $func_def{$fun1};
    map_file my $file_cat, $file_fun1;
    # Match /* ... */ ... (EOF|/|{|}|;)
    my @matches = ($file_cat =~ m/(\/\*(?:\*[^\/]|[^*])*\*\/[^\Z\/{}\;]*)/g);

    for my $i (@matches) {
        # There is exactly one end of comment, check that
        my $end_match = substr($i, index($i, "*/"));
        if (-1 != index($end_match, $fun1)) {
            $i = substr($i, 0, index($i, "*/"));
            return index($i, $fun2) != -1;
        }
    }
    print("No documentation found for $fun1\n");
    return 0;
}

sub linked_by_doc {
    my ($fun1, $fun2) = @_;
    return is_in_doc($fun1, $fun2) || is_in_doc($fun2, $fun1);
}

while(<>) {
    # funct pair: dma_map_sg_attrs crypto_tfm_clear_flags 1.869792
    if(/^funct pair: (\w+) (\w+) (\d+\.?\d+)$/) {
        if ($1 < $2) { # keep the functions in alphabetical order
            push(@{$score{$1}{$2}}, $3)
        } else {
            push(@{$score{$2}{$1}}, $3)
        }
    } elsif (/^Defining ([\w_\d]+) in file ([\w_\/\.\-]*)$/) {
        $func_def{$1} = $2;
    }
}

my @res;

for my $fun1 (keys %score) {
    #print $fun1 . "\n";
    for my $fun2 (keys %{$score{$fun1}}) {
        if ($func_def{$fun1} eq $func_def{$fun2}
            && scalar(@{$score{$fun1}{$fun2}}) > 5
            && linked_by_doc($fun1, $fun2)) {
            push(@res, avg(@{$score{$fun1}{$fun2}})
                 . " " . @{$score{$fun1}{$fun2}}
                 . " " . $fun1 . " " . $fun2 . " " . $func_def{$fun1});
        }
    }
}

for my $line (sort { $a <=> $b } @res) {
    print $line . "\n";
}
