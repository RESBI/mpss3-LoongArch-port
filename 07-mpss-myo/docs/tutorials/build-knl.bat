@echo off
REM Copyright 2012-2017 Intel Corporation.
REM
REM This file is subject to the Intel Sample Source Code License. A copy
REM of the Intel Sample Source Code License is included.
REM
REM build-knl.bat <dirname> <sourcefilename> <outputfilename>
REM
REM build-knl.bat builds the card-side of a myo tutorial using the Composer XE compiler.
REM

pushd %1
set LIBRARY_PATH=C:\Program Files (x86)\IntelSWTools\compilers_and_libraries_2017.0.070\linux\compiler\lib\intel64_lin
set MIC_GCC_VERSION=5.3.0
@echo on
icc -xmic-avx512 -platform=x86_64-linux -I../../../include %2 -o %3 -lscif -lpthread -lmyo-service
@echo off
popd
