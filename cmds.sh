#!/bin/bash
# From the Linux directory

SMATCH_DIR=$(dirname "$(realpath $0)")


env CHECK_APIS=TRUE $SMATCH_DIR/smatch_scripts/test_kernel.sh | tee log_apis
$SMATCH_DIR/parse_apis.pl /log_apis > $SMATCH_DIR/kernel_apis.h
make -C $SMATCH_DIR/ clean
make -C $SMATCH_DIR/ -j `nproc`
env CHECK_DEREF=TRUE $SMATCH_DIR/smatch_scripts/test_kernel.sh | tee log_api_deref
