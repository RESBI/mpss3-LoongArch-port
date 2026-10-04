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

# 移植参数说明：
#   -DMYOI_DIFF_I64  用标量差分层，避开 SSE2（源码自带该回退，未改代码）
#   -fcommon         GCC 10 起默认 -fno-common，老代码的暂定定义会重复
#   -lstdc++ -lm     库用 gcc 链接含 C++ 目标文件
MYO_FLAGS := -DMYOI_DIFF_I64 -fcommon -g -O2 -w

build:
	$(MAKE) -C src CC=$(CC) CXX=$(CXX) CFLAGS="$(MYO_FLAGS)" CXXFLAGS="$(MYO_FLAGS)" \
		LDFLAGS='-lstdc++ -lm' BUILD_AND_INSTALL_MAN_PAGES=0 INSTALL_TUTORIALS=0 \
		LIBRARY_PATH=$(STAGE)/lib64:$(PREFIX)/lib64

install: build
	install -d $(STAGE)/lib64 $(STAGE)/include
	install -m755 bin/libmyo-client.so.0 $(STAGE)/lib64/libmyo-client.so.0
	install -m755 bin/libmyo-service.so.0 $(STAGE)/lib64/libmyo-service.so.0 2>/dev/null || true
	ln -sf libmyo-client.so.0 $(STAGE)/lib64/libmyo-client.so
	cp -a include/. $(STAGE)/include/
	@if [ -z "$(DESTDIR)" ]; then ldconfig; fi
	@echo "已安装 libmyo-client.so.0 -> $(STAGE)/lib64"

clean:
	-$(MAKE) -C src clean
	-rm -rf bin
	-find . -name '*.o' -delete
