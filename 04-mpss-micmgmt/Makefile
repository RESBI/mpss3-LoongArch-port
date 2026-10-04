# ============================================================================
# MPSS 3.8.6 主机侧工具端 · LoongArch 移植版 · 发布版包装 Makefile
#
#   make            # 只构建
#   make install    # 构建并安装（默认 PREFIX=/usr，需要 root）
#   make install DESTDIR=/tmp/stage    # 装到暂存目录，不需要 root
#   make clean      # 清理构建产物
#
# 上游原始 Makefile 保留为 Makefile.mpss，本文件只做「按移植版验证过的参数
# 构建 + 按 MPSS 期望的路径安装」这两件事。
# ============================================================================

PREFIX  ?= /usr
DESTDIR ?=
CC      ?= gcc
CXX     ?= g++
STAGE   := $(DESTDIR)$(PREFIX)
RELROOT := $(CURDIR)/..

.PHONY: all build install clean
all: build

BUILD_ENV := C_INCLUDE_PATH=$(STAGE)/include CPLUS_INCLUDE_PATH=$(STAGE)/include \
             LIBRARY_PATH=$(STAGE)/lib64:$(PREFIX)/lib64
APPFLAGS  := CXXFLAGS='-Wno-error -fpermissive' CFLAGS='-Wno-error' \
             EXTRA_CFLAGS='-Wall -Wextra -Wno-error'

build:
	$(BUILD_ENV) $(MAKE) -C miclib CXX=$(CXX) CC=$(CC) CXXFLAGS='-Wno-error'
	$(BUILD_ENV) $(MAKE) -C apps/mpssinfo  CXX=$(CXX) CC=$(CC) $(APPFLAGS)
	$(BUILD_ENV) $(MAKE) -C apps/mpssflash CXX=$(CXX) CC=$(CC) $(APPFLAGS)
	$(BUILD_ENV) $(MAKE) -C apps/micsmc    CXX=$(CXX) CC=$(CC) $(APPFLAGS)

install: build
	install -d $(STAGE)/lib64 $(STAGE)/bin $(STAGE)/include
	install -m755 miclib/libs/libmicmgmt.so $(STAGE)/lib64/libmicmgmt.so.0.0.2
	ln -sf libmicmgmt.so.0.0.2 $(STAGE)/lib64/libmicmgmt.so.0
	ln -sf libmicmgmt.so.0.0.2 $(STAGE)/lib64/libmicmgmt.so
	install -m644 miclib/include/miclib.h $(STAGE)/include/miclib.h
	install -m644 miclib/include/ut_instr_event.h $(STAGE)/include/ut_instr_event.h
	install -m755 apps/mpssinfo/mpssinfo $(STAGE)/bin/mpssinfo
	-install -m755 apps/mpssflash/mpssflash $(STAGE)/bin/mpssflash
	-install -m755 apps/micsmc/micsmc $(STAGE)/bin/micsmc
	@if [ -z "$(DESTDIR)" ]; then ldconfig; fi
	@echo "已安装 libmicmgmt / mpssinfo / mpssflash / micsmc -> $(STAGE)"

clean:
	-$(MAKE) -C miclib clean
	-$(MAKE) -C apps/mpssinfo clean
	-$(MAKE) -C apps/mpssflash clean
	-$(MAKE) -C apps/micsmc clean
	-rm -rf miclib/objs miclib/libs build-tmp
	-find . -name '*.o' -delete
	-rm -f apps/mpssinfo/mpssinfo apps/mpssflash/mpssflash apps/mpssflash/flash1 apps/micsmc/micsmc
