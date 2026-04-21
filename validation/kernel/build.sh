#!/bin/bash

FILE=$1
FILE=$(echo ${FILE/.c/})

DIRNAME=$(dirname $0)

if [[ -e $DIRNAME/kernel_build_cfg ]] ; then
    source $DIRNAME/kernel_build_cfg
fi

if [[ ! -v KERNEL_DIR ]]; then
    KERNEL_DIR=/lib/modules/$(uname -r)/build
fi
if [[ ! -e $KERNEL_DIR ]] ; then
    echo "KERNEL_DIR ($KERNEL_DIR) not found."
    exit 1
fi

MOD_DIR=$(pwd)/kernel

make V=1 C=2 CHECK="../../smatch -p=kernel" -C $KERNEL_DIR M=$MOD_DIR ${FILE}.o | grep ^${FILE}.c
