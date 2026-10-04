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

CFLAGS_MPSS := -g -O2 -fpermissive
BUILD_ENV   := C_INCLUDE_PATH=$(STAGE)/include CPLUS_INCLUDE_PATH=$(STAGE)/include \
               LIBRARY_PATH=$(STAGE)/lib64:$(PREFIX)/lib64

build:
	$(BUILD_ENV) $(MAKE) -f Makefile.mpss all prefix=$(PREFIX) CC=$(CC) CXX=$(CXX) \
		CFLAGS="$(CFLAGS_MPSS)"

install: build
	install -d $(STAGE)/lib64 $(STAGE)/sbin $(STAGE)/include/mic
	install -m755 libmpssconfig/libmpssconfig.so $(STAGE)/lib64/libmpssconfig.so.0.0.1
	ln -sf libmpssconfig.so.0.0.1 $(STAGE)/lib64/libmpssconfig.so.0
	ln -sf libmpssconfig.so.0.0.1 $(STAGE)/lib64/libmpssconfig.so
	install -m644 libmpssconfig/mpssconfig.h $(STAGE)/include/mic/mpssconfig.h
	install -m644 libmpssconfig/libmpsscommon.h $(STAGE)/include/mic/libmpsscommon.h
	install -m755 mpssd/mpssd $(STAGE)/sbin/mpssd
	install -m4755 micctrl/micctrl $(STAGE)/sbin/micctrl
	@if [ -z "$(DESTDIR)" ]; then ldconfig; fi
	@echo "已安装 libmpssconfig / mpssd / micctrl（micctrl 带 setuid 位）-> $(STAGE)"

clean:
	-$(MAKE) -f Makefile.mpss clean
	-find . -name '*.o' -delete
	-rm -f libmpssconfig/libmpssconfig.so* mpssd/mpssd micctrl/micctrl micctrl/micctrl_passwd
