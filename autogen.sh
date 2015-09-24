#!/bin/sh
#
# autogen.sh: prepare build system
# Run this script to prepare the build-system for your local checkout. It
# requires the usual autotools dependencies to be installed.
#
# Prepare build system, but don't configure:
#   $ ./autogen.sh
#
# Prepare build system and run ./configure with debugging enabled:
#   $ ./autogen.sh c
#
# Prepare build system and run ./configure with llvm over gcc:
#   $ ./autogen.sh l
#
# You can put additional arguments to ./configure into ".config.args". They
# will be appended to the arguments used by this script.
#

set -e

oldpwd=$(pwd)
topdir=$(dirname $0)
cd $topdir

autoreconf --force --install --symlink

if [ -f "$topdir/.config.args" ]; then
        args="$args $(cat $topdir/.config.args)"
fi

cd $oldpwd

if [ "x$1" = "xc" ]; then
        $topdir/configure --enable-debug $args
        make clean
elif [ "x$1" = "xl" ]; then
        $topdir/configure CC=clang $args
        make clean
else
        echo
        echo "----------------------------------------------------------------"
        echo "Initialized build system. For a common configuration please run:"
        echo "----------------------------------------------------------------"
        echo
        echo "$topdir/configure $args"
        echo
fi
