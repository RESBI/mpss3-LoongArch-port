# ============================================================================
# MPSS 3.8.6 主机侧工具端 · LoongArch 移植版 · 发布版包装 Makefile
#
#   make            # 只构建（产出 mic.ko）
#   make install    # 构建并安装（默认 PREFIX=/usr，需要 root）
#   make install DESTDIR=/tmp/stage    # 装到暂存目录，不需要 root
#   make clean      # 清理构建产物
#
# 上游原始 Makefile 保留为 Makefile.mpss。调用它**必须显式 -f Makefile.mpss**：
# 否则 $(MAKE) 会再读本文件，无限递归（发布版自测时踩过一次，把内存吃满了）。
#
# 内核头文件：默认 /lib/modules/$(uname -r)/build，可用 KERNEL_SRC= 覆盖。
# ============================================================================

PREFIX         ?= /usr
DESTDIR        ?=
CC             ?= gcc
KERNEL_VERSION ?= $(shell uname -r)
KERNEL_SRC     ?= /lib/modules/$(KERNEL_VERSION)/build
SYSCONFDIR     ?= /etc
STAGE          := $(DESTDIR)$(PREFIX)
MPSS_MAKE      := $(MAKE) -f Makefile.mpss

.PHONY: all build install clean
all: build

build:
	$(MPSS_MAKE) MIC_CARD_ARCH=k1om KERNWARNFLAGS=-Wno-error \
		KERNEL_SRC=$(KERNEL_SRC) KERNEL_VERSION=$(KERNEL_VERSION)
	@echo "产出：$$(ls -l mic.ko 2>/dev/null | awk '{print $$5" 字节"}')"

install: build
	$(MPSS_MAKE) modules_install conf_install dev_install kdev_install \
		MIC_CARD_ARCH=k1om KERNEL_SRC=$(KERNEL_SRC) KERNEL_VERSION=$(KERNEL_VERSION) \
		prefix=$(PREFIX) sysconfdir=/etc INSTALL_MOD_PATH=$(DESTDIR) DESTDIR=$(DESTDIR)
	install -d $(DESTDIR)$(SYSCONFDIR)/modules-load.d
	install -m644 mic.modules-load $(DESTDIR)$(SYSCONFDIR)/modules-load.d/mic.conf
	@if [ -z "$(DESTDIR)" ]; then depmod -a $(KERNEL_VERSION); fi
	@echo "已安装 mic.ko -> /lib/modules/$(KERNEL_VERSION)/"
	@echo "  配置：$(SYSCONFDIR)/modprobe.d/mic.conf、$(SYSCONFDIR)/udev/rules.d/50-udev-mic.rules"
	@echo "        $(SYSCONFDIR)/modules-load.d/mic.conf（开机自动 modprobe mic）"

clean:
	-$(MPSS_MAKE) MIC_CARD_ARCH=k1om KERNEL_SRC=$(KERNEL_SRC) clean
	-find . \( -name '*.o' -o -name '*.ko' -o -name '.*.cmd' -o -name '*.mod' -o -name '*.mod.c' \) -delete
	-rm -rf .tmp_versions .cache.mk
	-rm -f mic.ko
