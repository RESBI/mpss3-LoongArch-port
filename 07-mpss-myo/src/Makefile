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

# This makefile performs five kinds of builds.
#
# 1. Non-Yocto local build (development build at a workstation)
# 2. Host KNC Yocto
# 3. Card KNC Yocto
# 4. Host KNL LB non-yocto.
# 5. Card KNL LB yocto.
#
# Before you can do a development build (#1 above), you must have already installed the MPSS SDK
# and the Intel Composer XE Compatability RPM.
#
# If it is not already installed, use the following command.
#
# sudo yum -y --nogpgcheck --noplugins --disablerepo=* --enablerepo=mpss-repo install mpss-sdk-k1om
# sudo yum -y --nogpgcheck --noplugins --disablerepo=* --enablerepo=mpss-repo install intel-composerxe-compat-k1om
#
# For local build (i.e. non yocto), use the following command line.
#
# make clean
# make TARGET_ICC='icc -mmic'
# sudo make TARGET_ICC='icc -mmic' install
#

BUILD_AND_INSTALL_MAN_PAGES=1
INSTALL_TUTORIALS=1
ifeq ($(origin CC),default)  # Local Case
#
# This is one of the two build types:
# 1. Non-Yocto local build (development build at a workstation)
# or:
# 4. Host KNL LB non-yocto.
#
HOST_CC = gcc
CARD_CC = $(TARGET_ICC)

ifeq ($(flow), cov)
HOST_CC = covc gcc
endif

#
# Warning, for build type 4, the 3 macros:
# TARGET, DBL_TARGET and BUILD_AND_INSTAL_MAN_PAGES
# are over-ridden on the command line for make.
#
TARGET = libmyo-client.so libmyo-service.so
DBL_TARGET = libmyodbl-client.so libmyodbl-service.so

BUILD_AND_INSTALL_MAN_PAGES=0
CARD = 1

# Define the following if you want to build with symbols for the MYO_BACKTRACE.
# For now, we only build this way for non-yocto builds.
#MYO_BACKTRACE_WANTED=1

ifdef KNL_VERSION
# This is for a build type 4 only:
DEFINE_KNL_VERSION = -DKNL_VERSION=\"$(KNL_VERSION)\"
MPSS_METADATA_C = myo_knl_version.c
endif

else # End of build types 1 and 4

HOST = 0
CARD = 0

ifeq ($(proj_target),knllb_card)
#
# This is the build type:
# 5. Card KNL LB yocto.
#
CARD = 1
TARGET_ARCH=knl-lb
ifdef KNL_VERSION
DEFINE_KNL_VERSION = -DKNL_VERSION=\"$(KNL_VERSION)\"
MPSS_METADATA_C = myo_knl_version.c
endif
else # ifeq knllb_card
ifeq (k1om,$(findstring k1om,$(CC))) # KNC Card-Side
#
# This is the build type:
# 3. Card KNC Yocto
#
CARD = 1
KNC = 1
TARGET_ARCH=k1om
MPSS_METADATA_PREFIX = ../
include mpss-metadata.mk
endif # ifeq k1om
endif # ifeq knllb_card

ifeq ($(CARD),1)
CARD_CC = $(TARGET_ICC)

TARGET = libmyo-service.so
DBL_TARGET = libmyodbl-service.so

YOCTO=1
BUILD_AND_INSTALL_MAN_PAGES=0
INSTALL_TUTORIALS=0

else #  $(CARD) 0

#
# This is for build type:
# 2. Host KNC Yocto
#

HOST_CC = $(CC)

YOCTO=1

TARGET = libmyo-client.so
DBL_TARGET = libmyodbl-client.so

endif # End build type 2
endif # End ifeq ($(origin CC),default)

#make conventions
SHELL = /bin/sh
srcdir = .

TARGETS = $(addprefix $(OUT_DIR)/,$(addsuffix .0,$(TARGET)))
DBL_TARGETS = $(addprefix $(OUT_DIR)/,$(addsuffix .0,$(DBL_TARGET)))

MYO_HOME = ..
OUT_DIR = $(MYO_HOME)/bin/

X1OM=$(MYO_HOME)/../drivers/cross/linux-k1om-4.7

MIC_GLIBC_ROOT=$(X1OM)/linux-$(TARGET_ARCH)

INCL_DIRS = -I$(MYO_HOME)/include \
	-I$(MYO_HOME)/src/include

#----------------------------------------------------#
MYO_CLIENT_CPP_SRCS = \
	machinedep/scif/host_scif_config.cpp machinedep/scif/elfparser.cpp  $(MYO_CPP_SRCS)
MYO_SERVICE_CPP_SRCS = \
	machinedep/scif/card_scif_config.cpp $(MYO_CPP_SRCS)

MYO_SRCS = myo.c \
	communication/myocomm.c \
	communication/myoscifcomm.c \
	communication/myoscifcomm_rma.c \
	communication/myowatchdog.c \
	machinedep/myoosplatform.c machinedep/threads/myothreads.c \
	allocator/myomemoryallocator.c allocator/myoplmemoryallocator.c \
	allocator/myoexmemoryallocator.c allocator/myoarenaallocator.c \
	allocator/myoexplmemoryallocator.c \
	pinnedmem/myopinnedmem.c pinnedmem/myopmsim.c \
	consistent/myoconsistent.c \
	consistent/myodiff.c consistent/myointernal.c\
	sync/myosync.c sync/myobarrier.c sync/myomutex.c sync/myosem.c \
	svar/myosvar.c \
	rfunc/myorfuncregister.c rfunc/myorfunc.c \
	misc/myovarray.c \
	misc/myostat.c misc/myoatomic.c misc/myoprint.c $(MPSS_METADATA_C)
#--tpbarrier removed for coverage purpose and currently unused--#
MYO_CLIENT_SRCS = $(MYO_SRCS)
MYO_SERVICE_SRCS = $(MYO_SRCS)

#Debugger Defines
HAS_DEBUGGER = -DHAS_DEBUGGER

DBL_C_SRCS = ../tools/dbl/myo_dbl.c $(MPSS_METADATA_C)

DBL_SERVICE_C_OBJS = $(DBL_C_SRCS:.c=.service.lrb.o)
DBL_CLIENT_C_OBJS = $(DBL_C_SRCS:.c=.client.lrb.o)

COMMON_DEFS = -DMYO_OVER_SCIF -DMYOI_DMA_COMM -DMYO_RPCMIC2MIC $(DEFINE_KNL_VERSION) #-DMYOI_FREEPHYMEM

# Add options for MYO performance data output.
ifdef MYO_STATS_WANTED
COMMON_DEFS += -DSTAT_SEND_TIME -DMYO_STATS
endif

# Add options to allow for a stack backtrace to be printed as part of the program debug.
ifdef MYO_BACKTRACE_WANTED
COMMON_DEFS += -g -rdynamic -DMYO_DEBUG_BACKTRACE
endif

# implement MYO over LRBN

# -------------------Host side --------------------- #
HOST_OBJ_SUFF   =       o

HOST_DEFS =

HOST_CXXFLAGS   =       $(CFLAGS) -g -pthread $(MPSS_METADATA_CFLAGS) -O3 -fPIC $(COMMON_DEFS) -DMULRECVTHREAD
HOST_CFLAGS     =       $(HOST_CXXFLAGS)
HOST_LDFLAGS    =       $(LDFLAGS) -pthread
HOST_LD_OFLAG   =       -o

ifdef MYO_BACKTRACE_WANTED
HOST_LDFLAGS += -g -rdynamic
endif


# ------------------- Card side --------------------- #

CARD_CXXFLAGS   =       $(CFLAGS) -g -pthread $(MPSS_METADATA_CFLAGS) -O3 -fPIC $(COMMON_DEFS) $(CARD_DEFS)
CARD_CFLAGS     =       $(CARD_CXXFLAGS)
CARD_LDFLAGS    =       $(LDFLAGS) -pthread -shared -static-intel -Wl,--version-script

ifeq ($(TARGET_ARCH),k1om)
CARD_CXXFLAGS+= -D_K1OM_
endif

ifdef MYO_COVERAGE
COMMON_DEFS += -DMYOI_FREEPHYMEM -DMYO_STATS #-DSTAT_SEND_TIME
CARD_CC := covc $(CARD_CC)
CARD_CXX := covc $(CARD_CXX)
endif

ifdef MYO_BACKTRACE_WANTED
CARD_LDFLAGS += -g -rdynamic
endif

MYO_SERVICE_OBJS = $(MYO_SERVICE_SRCS:.c=.service.o)
MYO_CLIENT_OBJS = $(MYO_CLIENT_SRCS:.c=.client.o)
MYO_SERVICE_CPP_OBJS = $(MYO_SERVICE_CPP_SRCS:.cpp=.service.o)
MYO_CLIENT_CPP_OBJS = $(MYO_CLIENT_CPP_SRCS:.cpp=.client.o)

#Headers
DEBUG_DIR = $(srcdir)/../tools/dbl
HEAD_DIR = $(srcdir)/../include
SRC_HEAD_DIR = $(srcdir)/include/
HEADERS = $(HEAD_DIR)/myo.h $(HEAD_DIR)/myoimpl.h $(HEAD_DIR)/myotypes.h $(DEBUG_DIR)/myo_dbl.h
ifdef TEST_SUP
HEADERS = $(SRC_HEAD_DIR)myoarena.h $(SRC_HEAD_DIR)myoconsistent.h \
	$(SRC_HEAD_DIR)myointernal.h $(SRC_HEAD_DIR)myoconfig.h \
	$(SRC_HEAD_DIR)myobasictypes.h $(SRC_HEAD_DIR)myoosplatform.h \
	$(SRC_HEAD_DIR)myolist.h $(SRC_HEAD_DIR)myosync.h \
	$(SRC_HEAD_DIR)myothreads.h $(SRC_HEAD_DIR)myotpbarrier.h \
	$(SRC_HEAD_DIR)myoexmemoryallocator.h \
	$(SRC_HEAD_DIR)myomemoryallocator.h \
	$(SRC_HEAD_DIR)myoexplmemoryallocator.h \
	$(SRC_HEAD_DIR)myoplmemoryallocator.h $(SRC_HEAD_DIR)myodiff.h \
	$(SRC_HEAD_DIR)myorfuncregister.h $(SRC_HEAD_DIR)myodebug.h \
	$(SRC_HEAD_DIR)myoprint.h $(SRC_HEAD_DIR)myostat.h \
	$(SRC_HEAD_DIR)myocomm.h $(SRC_HEAD_DIR)myotime.h
endif

# Documentation
DOCS = ../docs/MIC_MYO_API_Reference_Manual_0_2.pdf ../docs/MYO_Tutorial.pptx

# INSTALL Flags

TUTORIALS = $(srcdir)/../docs/tutorials/*

INSTALL = install
INSTALL_d = $(INSTALL) -d
INSTALL_x = $(INSTALL)
INSTALL_f = $(INSTALL) -m644
INSTALL_D = cp -dr --no-preserve=ownership

UNINSTALL = rm -f
UNINSTALL_r = rm -fr
UNINSTALL_D = rmdir --ignore-fail-on-non-empty

libdir = /usr/lib64
includedir = /usr/include
myodocdir = /usr/share/doc/myo
manpagedir = /usr/share/man
tutdir = $(myodocdir)/tutorials

ALL_TARGET = $(TARGET) $(DBL_TARGET)

ifeq ($(BUILD_AND_INSTALL_MAN_PAGES),1)
ALL_TARGET += man_pages
endif # BUILD_AND_INSTALL_MAN_PAGES

ifndef TEST_SUP
all : $(ALL_TARGET)
else
all :
endif # TEST_SUP

install :
ifndef TEST_SUP
	$(INSTALL_d) $(DESTDIR)$(libdir)
	$(INSTALL_x) $(TARGETS) $(DESTDIR)$(libdir)
	$(INSTALL_x) $(DBL_TARGETS) $(DESTDIR)$(libdir)
ifndef YOCTO
# For non-Yocto, build both the card and the host.
	cd $(DESTDIR)$(libdir) ; ln -s libmyo-client.so.0     libmyo-client.so
	cd $(DESTDIR)$(libdir) ; ln -s libmyodbl-client.so.0  libmyodbl-client.so
ifeq ($(CARD),1)
	cd $(DESTDIR)$(libdir) ; ln -s libmyo-service.so.0    libmyo-service.so
	cd $(DESTDIR)$(libdir) ; ln -s libmyodbl-service.so.0 libmyodbl-service.so
endif # CARD
else
# For Yocto, only build the card or the host.
ifeq ($(CARD),1)
	cd $(DESTDIR)$(libdir) ; ln -s libmyo-service.so.0    libmyo-service.so
	cd $(DESTDIR)$(libdir) ; ln -s libmyodbl-service.so.0 libmyodbl-service.so
endif # ifeq ($(CARD),1)
ifeq ($(CARD),0)
	cd $(DESTDIR)$(libdir) ; ln -s libmyo-client.so.0     libmyo-client.so
	cd $(DESTDIR)$(libdir) ; ln -s libmyodbl-client.so.0  libmyodbl-client.so
endif # ifeq ($(CARD),0)
endif # YOCTO
ifeq ($(INSTALL_TUTORIALS),1)
	$(INSTALL_d) $(DESTDIR)$(tutdir)
	$(INSTALL_D) $(TUTORIALS) $(DESTDIR)$(tutdir)
endif # INSTALL_TUTORIALS
	$(INSTALL_d) $(DESTDIR)$(includedir)
	$(INSTALL_f) $(HEADERS) $(DESTDIR)$(includedir)
ifeq ($(CARD),0)
	$(INSTALL_d) $(DESTDIR)$(myodocdir)
	$(INSTALL_f) $(DOCS) $(DESTDIR)$(myodocdir)
	$(INSTALL_d) $(DESTDIR)$(manpagedir)
	$(INSTALL_D) ../docs/tools/ExtractManPages/man3 $(DESTDIR)$(manpagedir)
endif # CARD,0
ifdef LLAPI
	$(INSTALL_d) $(DESTDIR)$(includedir)
	$(INSTALL_f) $(HEADERS) $(DESTDIR)$(includedir)
	$(INSTALL_d) $(DESTDIR)$(tutdir)
	$(INSTALL_D) $(TUTORIALS) $(DESTDIR)$(tutdir)
	$(INSTALL_d) $(DESTDIR)$(myodocdir)
	$(INSTALL_f) $(DOCS) $(DESTDIR)$(myodocdir)
endif # LLAPI
else # TEST_SUP
	$(INSTALL_d) $(DESTDIR)$(includedir)
	$(INSTALL_f) $(HEADERS) $(DESTDIR)$(includedir)
endif # TEST_SUP

uninstall :
	$(UNINSTALL) $(addprefix $(DESTDIR)$(libdir)/,$(TARGET)) $(addprefix $(DESTDIR)$(libdir)/,$(DBL_TARGET))
	$(UNINSTALL_D) $(DESTDIR)$(libdir)
	$(UNINSTALL) $(addprefix $(DESTDIR)$(includedir)/,$(HEADERS))
	$(UNINSTALL_D) $(DESTDIR)$(includedir)
	$(UNINSTALL_r) $(DESTDIR)$(tutdir)
	$(UNINSTALL) $(addprefix $(DESTDIR)$(myodocdir)/,$(DOCS))
	$(UNINSTALL_D) $(DESTDIR)$(myodocdir)
ifdef LLAPI
	$(UNINSTALL) $(addprefix $(DESTDIR)$(includedir),$(HEADERS))
	$(UNINSTALL_D) $(DESTDIR)$(includedir)
	$(UNINSTALL_r) $(DESTDIR)$(tutdir)
	$(UNINSTALL) $(addprefix $(DESTDIR)$(myodocdir),$(DOCS))
	$(UNINSTALL_D) $(DESTDIR)$(myodocdir)
endif

$(MYO_SERVICE_OBJS): %.service.o: %.c
	$(CARD_CC) $(CARD_CXXFLAGS) -DMYO_MIC_CARD $(INCL_DIRS) -c $^ -o $@

$(MYO_CLIENT_OBJS): %.client.o: %.c
	$(HOST_CC) $(HOST_CXXFLAGS) -DMYO_CPU $(INCL_DIRS) -c $^ -o $@

$(MYO_SERVICE_CPP_OBJS): %.service.o: %.cpp
	$(CARD_CC) $(CARD_CXXFLAGS) -DMYO_MIC_CARD $(INCL_DIRS) -c $^ -o $@

$(MYO_CLIENT_CPP_OBJS): %.client.o: %.cpp
	$(HOST_CC) $(HOST_CXXFLAGS) -DMYO_CPU $(INCL_DIRS) -c $^ -o $@

# Rules for building shared library

libmyo-service.so : libmyo-service.so.0
	#ln -s $(OUT_DIR)libmyo-service.so.0 $(OUT_DIR)$@

libmyo-client.so : libmyo-client.so.0
	#ln -s $(OUT_DIR)libmyo-client.so.0 $(OUT_DIR)$@

libmyo-service.so.0 : $(MYO_SERVICE_OBJS) $(MYO_SERVICE_CPP_OBJS)
	$(CARD_CC) $(CARD_LDFLAGS) linker_script.map -Wl,-soname,$@ $^ -o $@ -lscif
	mkdir -p $(OUT_DIR)
	mv $@ $(OUT_DIR)

libmyo-client.so.0: $(MYO_CLIENT_OBJS) $(MYO_CLIENT_CPP_OBJS)
	$(HOST_CC) $(HOST_LDFLAGS) $^ -o $@ -lscif -shared -Wl,-soname,$@ -Wl,--version-script linker_script.map
	mkdir -p $(OUT_DIR)
	mv $@ $(OUT_DIR)

$(DBL_SERVICE_C_OBJS): %.service.lrb.o: %.c
	$(CARD_CC) $(CARD_CFLAGS) $(HAS_DEBUGGER) $(INCL_DIRS) -fPIC  -c $^ -o $@
$(DBL_CLIENT_C_OBJS): %.client.lrb.o: %.c
	$(CC) $(HOST_CFLAGS) $(HAS_DEBUGGER) $(INCL_DIRS) -fPIC -c $^ -o $@

libmyodbl-service.so : libmyodbl-service.so.0
	#ln -s $(OUT_DIR)libmyodbl-service.so.0 $(OUT_DIR)$@

libmyodbl-client.so : libmyodbl-client.so.0
	#ln -s $(OUT_DIR)libmyodbl-client.so.0 $(OUT_DIR)$@

libmyodbl-service.so.0 : $(DBL_SERVICE_C_OBJS)
	$(CARD_CC) $(CARD_LDFLAGS) ../tools/dbl/linker_script.map -Wl,-soname,$@ $^ -o $@
	mkdir -p $(OUT_DIR)
	mv $@ $(OUT_DIR)

libmyodbl-client.so.0: $(DBL_CLIENT_C_OBJS)
	$(CC) $(HOST_LDFLAGS) $^ -o $@ -shared -Wl,-soname,$@ -Wl,--version-script ../tools/dbl/linker_script.map
	mkdir -p $(OUT_DIR)
	mv $@ $(OUT_DIR)

clean:
	/bin/rm -f $(OUT_DIR)/libmyo$(SIM)-service.a $(OUT_DIR)/libmyo$(SIM)-client.a $(MYO_SERVICE_OBJS) $(MYO_CLIENT_OBJS) $(MYO_CLIENT_CPP_OBJS) $(MYO_SERVICE_CPP_OBJS)
	/bin/rm -f $(OUT_DIR)/libmyo-*.so*
	/bin/rm -f $(DBL_CLIENT_C_OBJS) $(DBL_SERVICE_C_OBJS) $(OUT_DIR)/libmyodbl-*.so*

clean-obj:
	/bin/rm -f $(MYO_SERVICE_OBJS) $(MYO_CLIENT_OBJS) $(MYO_CLIENT_CPP_OBJS) $(MYO_SERVICE_CPP_OBJS)

man_pages:
	cd ../docs/tools/ExtractManPages ; make
