#!/bin/bash
# From the Linux directory

SMATCH_DIR=$(dirname "$(realpath $0)")

# To prevent races when writing to the output, each thread has its own file
# and outputs are fusioned together afterwards. 
rm -rf log_apis
env CHECK_APIS=TRUE OUTFILE=log_apis $SMATCH_DIR/smatch_scripts/test_kernel.sh
cat log_apis/* > log_apis_cat
$SMATCH_DIR/parse_apis.pl log_apis_cat > $SMATCH_DIR/kernel_apis.inc.h
make -C $SMATCH_DIR/ clean
make -C $SMATCH_DIR/ -j `nproc`
rm -rf log_apis_args
env CHECK_DEREF=TRUE OUTFILE=log_apis_args $SMATCH_DIR/smatch_scripts/test_kernel.sh
cat log_apis_args/* > log_apis_args_cat
$SMATCH_DIR/parse_apis_args.pl log_apis_cat

