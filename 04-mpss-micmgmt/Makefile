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

# apps/* 需要本包**源码树内**的头与库（miclib/include/miclib.h、miclib/libs/libmicmgmt.so），
# 否则就形成"apps 依赖 install 产物"的鸡生蛋：install: build，而 headers/库只有 install 才到位。
# 注意：这里只给 apps 用，miclib 自身仍用 SEARCH_ENV —— 避免把 /usr/include 重复加入
# C_INCLUDE_PATH 而破坏 libstdc++ 的 #include_next <stdlib.h>（见上方注释）。
APPSEARCH   := C_INCLUDE_PATH=$(CURDIR)/miclib/include CPLUS_INCLUDE_PATH=$(CURDIR)/miclib/include \
               LIBRARY_PATH=$(CURDIR)/miclib/libs

.PHONY: all build install clean
all: build

APPFLAGS  := CXXFLAGS='-Wno-error -fpermissive' CFLAGS='-Wno-error' \
             EXTRA_CFLAGS='-Wall -Wextra -Wno-error -I$(CURDIR)/miclib/include'

build:
	$(SEARCH_ENV) $(MAKE) -C miclib CXX=$(CXX) CC=$(CC) CXXFLAGS='-Wno-error'
	$(APPSEARCH) $(MAKE) -C apps/mpssinfo  CXX=$(CXX) CC=$(CC) $(APPFLAGS)
	$(APPSEARCH) $(MAKE) -C apps/mpssflash CXX=$(CXX) CC=$(CC) $(APPFLAGS)
	$(APPSEARCH) $(MAKE) -C apps/micsmc    CXX=$(CXX) CC=$(CC) $(APPFLAGS)

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

uninstall:
	rm -f $(STAGE)/lib64/libmicmgmt.so.0.0.2 $(STAGE)/lib64/libmicmgmt.so.0 $(STAGE)/lib64/libmicmgmt.so
	rm -f $(STAGE)/include/miclib.h $(STAGE)/include/ut_instr_event.h
	rm -f $(STAGE)/bin/mpssinfo $(STAGE)/bin/mpssflash $(STAGE)/bin/micsmc
	@if [ -z "$(DESTDIR)" ]; then ldconfig; fi
	@echo "已卸载 libmicmgmt / mpssinfo / mpssflash / micsmc"
