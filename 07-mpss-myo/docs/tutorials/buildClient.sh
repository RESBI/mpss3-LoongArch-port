
# Copyright 2012-2017 Intel Corporation.
#
# This file is subject to the Intel Sample Source Code License. A copy
# of the Intel Sample Source Code License is included.
#
# buildClient.sh <dirname> <sourcefilename> <outputfilename>
#
# buildClient.sh builds the host-side of a myo tutorial using the g++ compiler.
#

pushd $1

g++ $2 -o $3 -lscif -lpthread -lmyo-client

popd
