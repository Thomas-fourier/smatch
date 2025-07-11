#!/bin/bash

NR_CPU=$(nproc)
TARGET=""
WLOG="smatch_warns.txt"
LOG="smatch_compile.warns"
PROJECT="smatch_generic"

function usage {
    echo
    echo "Usage: $(basename $0) [smatch options]"
    echo "Compiles the kernel with -j${NR_CPU}"
    echo " available options:"
    echo "	--endian          : enable endianness check"
    echo "	--target {TARGET} : specify build target, default: $TARGET"
    echo "	--log {FILE}      : Output compile log to file, default is: $LOG"
    echo "	--wlog {FILE}     : Output warnings to file, default is: $WLOG"
    echo "      -p <project>      : Specify project to use, default is: $PROJECT"
    echo "	--help            : Show this usage"
    exit 1
}


while true ; do
    if [[ "$1" == "--endian" ]] ; then
	ENDIAN="CF=-D__CHECK_ENDIAN__"
	shift
    elif [[ "$1" == "--target" ]] ; then
	shift
	TARGET="$1"
	shift
    elif [[ "$1" == "--log" ]] ; then
	shift
	LOG="$1"
	shift
    elif [ "$1" == "-p" ] || [ "$1" == "--project" ] ; then
        shift
        PROJECT="$1"
        shift
    elif [[ "$1" == "--wlog" ]] ; then
	shift
	WLOG="$1"
	shift
    elif [[ "$1" == "--help" ]] ; then
	usage
    else
	    break
    fi
done

SCRIPT_DIR=$(dirname $0)
if [ -e $SCRIPT_DIR/../smatch ] ; then
    cp $SCRIPT_DIR/../smatch $SCRIPT_DIR/../bak.smatch
    CMD=$SCRIPT_DIR/../bak.smatch
    BIN_DIR=$SCRIPT_DIR/../
elif which smatch | grep smatch > /dev/null ; then
    CMD=smatch
else
    echo "Smatch binary not found."
    exit 1
fi

if [[ ! -z $ARCH ]]; then
	KERNEL_ARCH="ARCH=$ARCH"
fi
if [[ ! -z $CROSS_COMPILE ]] ; then
	KERNEL_CROSS_COMPILE="CROSS_COMPILE=$CROSS_COMPILE"
fi
if [[ ! -z $O ]] ; then
	KERNEL_O="O=$O"
fi

make $KERNEL_ARCH $KERNEL_CROSS_COMPILE $KERNEL_O clean
find -name \*.c.smatch -exec rm \{\} \;
make $KERNEL_ARCH $KERNEL_CROSS_COMPILE $KERNEL_O -j${NR_CPU} $ENDIAN -k CC=$BIN_DIR/cgcc CHECK="$CMD -p=${PROJECT} --full-path --file-output $*" \
	C=1 $TARGET 2>&1 | tee $LOG
find -name \*.c.smatch -exec cat \{\} \; -exec rm \{\} \; > $WLOG

echo "Done.  The warnings are saved to $WLOG"
