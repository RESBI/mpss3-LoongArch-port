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

#!/bin/sh

COI_MAJOR_VERSION=$1
COI_MINOR_VERSION=$2
SDK_PATH=`readlink -f $3`
BUILD_PATH=`readlink -f $4`
DOXYGEN_EXE=$5
TEX_BIN_PATH=$6
DOCS_PATH=`readlink -f $BUILD_PATH/docs`

COI_VERSION=$COI_MAJOR_VERSION'_'$COI_MINOR_VERSION
COI_FILENAME='MIC_COI_API_Reference_Manual_'$COI_VERSION
COI_FILENAME_WITH_EXTENSION=$COI_FILENAME'.pdf'

if [ $# -ne 6 ]
then
echo Usage: build_docs.sh \<major version number\> \<minor version number\> \<sdk path\> \<build path\> \<doxygen_exe_path\> \<tex_bin_dir\>
echo        \(Linux\) e.g. ./build_docs.sh 0 2 /home/user/depot/AVC/COI_SDK_Core /home/user/depot/AVC/COI_SDK_Core/build_cmake doxygen /usr/bin
echo        \(cygwin\) e.g. ./build_docs.sh 0 2 /home/user/depot/AVC/COI_SDK_Core /home/user/depot/AVC/COI_SDK_Core/build_cmake /home/user/depot/AVC/COI_SDK_Core/imports/doxygen /home/user/depot/AVC/COI_SDK_Core/imports/MiKTEX_2_9/miktex/bin
exit
fi

pwd
mkdir -p $DOCS_PATH/temp
rm -fr $DOCS_PATH/temp/*
cd $DOCS_PATH/temp

echo "/** @mainpage MIC COI API Reference Manual $COI_MAJOR_VERSION.$COI_MINOR_VERSION" > _MIC_COI_API_Reference_Manual.txt

cat $SDK_PATH/docs_config/coi_dox.txt >> _MIC_COI_API_Reference_Manual.txt


cat $SDK_PATH/include/common/COIResult_common.h > COIResult_common.h
cat _MIC_COI_API_Reference_Manual.txt >> COIResult_common.h

mkdir -p $COI_FILENAME

cat $SDK_PATH/docs_config/coi_dox_cfg.txt | sed s/MAJOR/$COI_MAJOR_VERSION/ | sed s/MINOR/$COI_MINOR_VERSION/ > coi_dox_cfg.tmp

if [[ $OSTYPE == 'cygwin' ]] ; then 
 cat coi_dox_cfg.tmp | sed s,SDK_PATH,`cygpath -m $SDK_PATH`, > coi_dox_cfg.txt
else
 cat coi_dox_cfg.tmp | sed s,SDK_PATH,$SDK_PATH, > coi_dox_cfg.txt
fi

cat $SDK_PATH/docs_config/footer.html | sed s/MAJOR/$COI_MAJOR_VERSION/ | sed s/MINOR/$COI_MINOR_VERSION/ > footer.html
cat $SDK_PATH/docs_config/header.html | sed s/MAJOR/$COI_MAJOR_VERSION/ | sed s/MINOR/$COI_MINOR_VERSION/ > header.html

mkdir -p $COI_FILENAME/latex
rm -fr $COI_FILENAME/latex/*

echo "Running doxygen..."
$DOXYGEN_EXE coi_dox_cfg.txt > /dev/null

# Copy all our new man files
mkdir -p ../../coi/docs/man/man3
cp $COI_FILENAME/man/man3/* ../../coi/docs/man/man3

cd $COI_FILENAME/latex

cat $SDK_PATH/docs_config/doxygen.sty | sed s/MAJOR/$COI_MAJOR_VERSION/ | sed s/MINOR/$COI_MINOR_VERSION/ > ./doxygen.sty

echo "Starting tex manual layout..."
$TEX_BIN_PATH/latex refman.tex > /dev/null 

echo "Creating index..."
$TEX_BIN_PATH/makeindex -q refman.idx > /dev/null
cat refman.ilg > ../_warnings_makeindex.txt 

echo "Creating pdf file..."
$TEX_BIN_PATH/pdflatex refman.tex > ../_warnings_latex.txt 

# Rename to the final filename.  We wait to do this rather than use the
# -job options for pdflatex.exe or earlier commands because the latex
# pipeline assumes the refman.* name for all files (refman.tex, refman.idx,
# etc.) and if you change any of them then the index or other parts of
# the document do not work correctly.
cat refman.pdf > $DOCS_PATH/$COI_FILENAME_WITH_EXTENSION

cd $DOCS_PATH
rm -fr temp
cp $COI_FILENAME_WITH_EXTENSION ../coi/docs

