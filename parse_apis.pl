#!/usr/bin/perl
use strict;
use warnings;
use Data::Dumper;

# See check_apis.c for usage

# Do not show APIs with less than this number of implementations
my $min_number_of_instances = 10;

my $current_struct = "";
my $current_struct_name = "";
my $current_struct_counted = 0;
my $current_file = "";
my %struct_count = (); # API_name -> count
my %struct_to_funcs = (); # API_name -> [file, API member, function]

while(<>) {
#API 'struct amdgpu_irq_src_funcs' 'xgpu_ai_mailbox_ack_irq_funcs' decl 72 to 79 file drivers/irqchip/irq-madera.c
#API - xgpu_ai_mailbox_ack_irq_funcs.process = xgpu_ai_mailbox_ack_irq
#API - xgpu_ai_mailbox_ack_irq_funcs.set = xgpu_ai_set_mailbox_ack_irq
    if(/API 'struct (\w+)' '(\w+)' decl (\d+) to (\d+) file (.*)/) {
        $current_struct = $1;
        $current_struct_name = $2;
        $current_struct_counted = 0;
        $current_file = $5;
    } elsif(/API - (\w+)\.(\w+) = (\w+)/) {
        # Only count when the API is actually implemented, i.e., has some member functions
        if(!$current_struct_counted) {
            $current_struct_counted = 1;
            $struct_count{$current_struct}++;
        }
        push(@{$struct_to_funcs{$current_struct}}, [$current_struct_name, $current_file, $2, $3]);
    }
}


for my $s (sort keys %struct_to_funcs) {
    next if($struct_count{$s} < $min_number_of_instances);
    for my $f (@{$struct_to_funcs{$s}}) {
        print "\t{ \"$s\", \"$f->[0]\", \"$f->[1]\", \"$f->[2]\", \"$f->[3]\"},\n";
    }
}