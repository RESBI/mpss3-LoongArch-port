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

TOOLS := $(RELROOT)/00-build-tools

build:
	PATH="$(TOOLS):$$PATH" $(MAKE) -f Makefile.mpss libscif.so CC=$(CC) \
		MPSS_METADATA_PREFIX=./ CFLAGS="-g -O2"

install: build
	install -d $(STAGE)/lib64 $(STAGE)/include
	install -m755 libscif.so $(STAGE)/lib64/libscif.so.0.0.1
	ln -sf libscif.so.0.0.1 $(STAGE)/lib64/libscif.so.0
	ln -sf libscif.so.0.0.1 $(STAGE)/lib64/libscif.so
	install -m644 scif.h $(STAGE)/include/scif.h
	install -m644 scif_ioctl.h $(STAGE)/include/scif_ioctl.h
	@if [ -z "$(DESTDIR)" ]; then ldconfig; fi
	@echo "已安装 libscif -> $(STAGE)/lib64/libscif.so.0.0.1 与 $(STAGE)/include/scif.h"

clean:
	-$(MAKE) -f Makefile.mpss clean
	-rm -f libscif.so libscif.so.0 libscif.map
	-find . -maxdepth 1 -name '*.o' -delete
