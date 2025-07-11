#!/bin/bash

file=$1
project=$(echo "$2" | cut -d = -f 2)

if [[ "$file" = "" ]] ; then
    echo "Usage:  $(basename $0) <file with smatch messages> -p=<project>"
    exit 1
fi

if [[ "$project" != "kernel" ]] ; then
    exit 0
fi

outfile="kernel.dma_funcs"
bin_dir=$(dirname $0)
remove=$(echo ${bin_dir}/../smatch_data/${outfile}.remove)
tmp=$(mktemp /tmp/smatch.XXXX)
tmp2=$(mktemp /tmp/smatch.XXXX)

echo "// list of DMA function and buffer parameters." > $outfile
echo '// generated by `gen_dma_funcs.sh`' >> $outfile
${bin_dir}/trace_params.pl $file usb_control_msg 6 >> $tmp
${bin_dir}/trace_params.pl $file usb_fill_bulk_urb 3 >> $tmp
${bin_dir}/trace_params.pl $file dma_map_single 1 >> $tmp
cat $tmp | sort -u > $tmp2
mv $tmp2 $tmp
cat $tmp $remove $remove 2> /dev/null | sort | uniq -u >> $outfile
rm $tmp
echo "Done.  List saved as '$outfile'"
