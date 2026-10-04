# Copyright 2010-2017 Intel Corporation.
#
# This library is free software; you can redistribute it and/or modify it
# under the terms of the GNU Lesser General Public License as published
# by the Free Software Foundation, version 2.1.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
# Lesser General Public License for more details.
#
# Disclaimer: The codes contained in these modules may be specific
# to the Intel Software Development Platform codenamed Knights Ferry,
# and the Intel product codenamed Knights Corner, and are not backward
# compatible with other Intel products. Additionally, Intel will NOT
# support the codes or instruction set in future products.
#
# Intel offers no warranty of any kind regarding the code. This code is
# licensed on an "AS IS" basis and Intel is not obligated to provide
# any support, assistance, installation, training, or other services
# of any kind. Intel is also not obligated to provide any updates,
# enhancements or extensions. Intel specifically disclaims any warranty
# of merchantability, non-infringement, fitness for any particular
# purpose, and any other warranty.
#
# Further, Intel disclaims all liability of any kind, including but
# not limited to liability for infringement of any proprietary rights,
# relating to the use of the code, even if Intel is notified of the
# possibility of such liability. Except as expressly stated in an Intel
# license agreement provided with this code and agreed upon with Intel,
# no license, express or implied, by estoppel or otherwise, to any
# intellectual property rights is granted herein.

#
# This script generates a stub library given a shared library.
#
# The stub will have the same versioned symbols (matching a pattern)
# as input library. The goal is to preseve original ABI but provide
# a library that has no dependencies on other libraries.
# This way, linking against produced stubs is easier, as compilation
# can be performed in environment where full library would not be able
# to satisfy all of its dependencies.
#
# Input:
#
# COMPAT_LIB_NAME - name of the library
# COMPAT_LIB_PATTERN - namespace 
# COMPAT_INPUT_DIR - input directory where COMPAT_LIB_NAME is located
# COMPAT_OUTPUT_DIR - output directory


#get_text_section_number $LIBRARY
function get_text_section_number {
    readelf -SW $1 | grep ' \.text ' | sed 's:^.*\[\(.*\)\].*:\1:'
}

#get_soname $LIBRARY
function get_soname {
    readelf --dynamic -W $1 | grep 'Library soname' | sed 's:^.*\[\(.*\)\]:\1:'
}

#get_symbols $LIBRARY $PATTERN $TEXT_SECTION
function get_symbols() {
    readelf -W --dyn-syms $1 | grep "$3 $2" | sed "s:^.* $3 ::"
}

function do_compat_lib {
    ACTION=$1
    LIBRARY=$2
    PATTERN=$3
    TEXT_SECTION=$4

    if [ "$ACTION" == "src" ]
    then
        cat <<STUB_SOURCE
        #define CONCAT2(x, y, z) x ## y ## z
        #define CONCAT(x, y, z) CONCAT2(x, y, z)

        #define STRINGIZE2(x) #x
        #define STRINGIZE(x) STRINGIZE2(x)

        #define SYMBOL(name, ver) void CONCAT(mpss_private_, name, __LINE__) () {} \
        __asm__(".symver " STRINGIZE(CONCAT(mpss_private_, name, __LINE__)) "," #name ver);
STUB_SOURCE

        get_symbols $LIBRARY $PATTERN $TEXT_SECTION | sed 's:\(^[^@]*\)\(.*\):SYMBOL(\1,"\2"):'
    elif [ "$ACTION" == "map" ]
    then
        echo "MPSS_PRIVATE { local: mpss_private_*; };"
        get_symbols $LIBRARY $PATTERN $TEXT_SECTION | sed 's:^.*@\(.*\):\1 { };:' | sort -u
    fi
}

if [ "$COMPAT_INPUT_DIR" == "" ] || [ "$COMPAT_OUTPUT_DIR" == "" ] || [ "$COMPAT_LIB_NAME" == "" ] || [ "$COMPAT_LIB_PATTERN" == "" ] 
then
    echo "Failed to create compat library! Bad arguments."
    exit -1
fi

echo "COMPAT_INPUT_DIR   = $COMPAT_INPUT_DIR"
echo "COMPAT_OUTPUT_DIR  = $COMPAT_OUTPUT_DIR"
echo "COMPAT_LIB_NAME    = $COMPAT_LIB_NAME"
echo "COMPAT_LIB_PATTERN = $COMPAT_LIB_PATTERN"

TEXT_SECTION=`get_text_section_number $COMPAT_INPUT_DIR/$COMPAT_LIB_NAME`
SONAME=`get_soname $COMPAT_INPUT_DIR/$COMPAT_LIB_NAME`

do_compat_lib src $COMPAT_INPUT_DIR/$COMPAT_LIB_NAME $COMPAT_LIB_PATTERN $TEXT_SECTION > $COMPAT_OUTPUT_DIR/src.c
do_compat_lib map $COMPAT_INPUT_DIR/$COMPAT_LIB_NAME $COMPAT_LIB_PATTERN $TEXT_SECTION > $COMPAT_OUTPUT_DIR/src.map

if [ "x$SONAME" != "x" ]
then
    SONAME_ARG="-Wl,-soname=$SONAME"
fi

gcc -shared -fpic $SONAME_ARG $COMPAT_OUTPUT_DIR/src.c -Wl,--version-script=$COMPAT_OUTPUT_DIR/src.map -o $COMPAT_OUTPUT_DIR/$COMPAT_LIB_NAME.compat
strip $COMPAT_OUTPUT_DIR/$COMPAT_LIB_NAME.compat
