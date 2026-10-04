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

# 编译器搜索路径：只有「暂存安装」或「非 /usr 前缀」才需要显式指点。
# 切记：PREFIX=/usr 时不要把这些目录塞进 C_INCLUDE_PATH/CPLUS_INCLUDE_PATH ——
# /usr/include 已是系统目录，重复一次会让 libstdc++ 的
#   #include_next <stdlib.h>
# 跳过去、报 "stdlib.h: No such file or directory"。
ifeq ($(DESTDIR),)
  ifeq ($(PREFIX),/usr)
    EXTRA_INC :=
    EXTRA_LIB :=
  else
    EXTRA_INC := $(PREFIX)/include
    EXTRA_LIB := $(PREFIX)/lib64
  endif
else
  EXTRA_INC := $(DESTDIR)$(PREFIX)/include
  EXTRA_LIB := $(DESTDIR)$(PREFIX)/lib64
endif
SEARCH_ENV := C_INCLUDE_PATH=$(EXTRA_INC) CPLUS_INCLUDE_PATH=$(EXTRA_INC) LIBRARY_PATH=$(EXTRA_LIB)

.PHONY: all build install clean
all: build

APPFLAGS  := CXXFLAGS='-Wno-error -fpermissive' CFLAGS='-Wno-error' \
             EXTRA_CFLAGS='-Wall -Wextra -Wno-error'

build:
	$(SEARCH_ENV) $(MAKE) -C miclib CXX=$(CXX) CC=$(CC) CXXFLAGS='-Wno-error'
	$(SEARCH_ENV) $(MAKE) -C apps/mpssinfo  CXX=$(CXX) CC=$(CC) $(APPFLAGS)
	$(SEARCH_ENV) $(MAKE) -C apps/mpssflash CXX=$(CXX) CC=$(CC) $(APPFLAGS)
	$(SEARCH_ENV) $(MAKE) -C apps/micsmc    CXX=$(CXX) CC=$(CC) $(APPFLAGS)

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
