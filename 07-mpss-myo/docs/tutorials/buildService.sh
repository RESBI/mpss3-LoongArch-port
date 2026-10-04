
# Copyright 2012-2017 Intel Corporation.
#
# This file is subject to the Intel Sample Source Code License. A copy
# of the Intel Sample Source Code License is included.
#
# buildService.sh <dirname> <sourcefilename> <outputfilename>
#
# buildService.sh builds the card-side of a myo tutorial using the Composer XE compiler.
#

pushd $1

$CC $CFLAGS $LDFLAGS $2 -o $3 -lscif -lpthread -lmyo-service

popd
