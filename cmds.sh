#!/bin/bash
# From the Linux directory

SMATCH_DIR=$(dirname "$(realpath $0)")

# To prevent races when writing to the output, each thread has its own file
# and outputs are fusioned together afterwards. 
env CHECK_APIS=TRUE OUTFILE=log_api $SMATCH_DIR/smatch_scripts/test_kernel.sh
cat log_api/* > log_api_cat
$SMATCH_DIR/parse_apis.pl log_api_cat > $SMATCH_DIR/kernel_api.inc.h
make -C $SMATCH_DIR/ clean
make -C $SMATCH_DIR/ -j `nproc`
env CHECK_DEREF=TRUE OUTFILE=log_api_args $SMATCH_DIR/smatch_scripts/test_kernel.sh
cat log_api_args/* > log_api_args_cat
$SMATCH_DIR/parse_api_args.pl log_api_cat

