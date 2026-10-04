#!/usr/bin/env python3
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
import sys
import os
from optparse import OptionParser

FW_VERSION_FILE = "FW_Ver.h"

def extract(msg, id):
    sidx = msg.find(id)
    sidx = msg.find("'", sidx) + 1
    eidx = msg.find("'", sidx)
    return msg[sidx:eidx]

def main():
    parser = OptionParser()
    parser.add_option("-s", "--smc_fw", dest="smc_fw", action="store_true",
                      help="get smc_fw version")
    parser.add_option("-f", "--flash", dest="flash", action="store_true",
                      help="get flash spi version")
    parser.add_option("-b", "--smc_bl", dest="smc_bl", action="store_true",
                      help="get smc bootloader version")
    parser.add_option("-p", "--path", dest="path", action="store", default=".",
                      help="path where we should look for {0} file".format(FW_VERSION_FILE))

    (options, args) = parser.parse_args()

    with open("{0}{1}{2}".format(options.path,
	                         os.sep,
				 FW_VERSION_FILE), "r") as content:
        text = content.read()

    if options.smc_fw:
        print("{0}.{1}.{2}".format(extract(text, "SMC_Ver_Major"),
                                   extract(text, "SMC_Ver_Minor"),
                                   extract(text, "SMC_Ver_Build")))
    elif options.flash:
        print("{0}.{1}.{2}.{3}".format(extract(text, "SPI_Ver_Major"),
                                   extract(text, "SPI_Ver_Minor"),
                                   extract(text, "SPI_Ver_HotFix"),
                                   extract(text, "SPI_Ver_Release")))
    elif options.smc_bl:
        print("{0}.{1}.{2}".format(extract(text, "SMC_Bootloader_Ver_Major"),
                                   extract(text, "SMC_Bootloader_Ver_Minor"),
                                   extract(text, "SMC_Bootloader_Ver_Build")))

    return 0

if __name__ == "__main__":
    STATUS = main()
    sys.exit(STATUS)
