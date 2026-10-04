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
	install -d $(STAGE)/libexec/mpss $(STAGE)/lib/systemd/system
	install -m755 mic0-up.sh $(STAGE)/libexec/mpss/mic0-up.sh
	install -m644 mic0-net.service $(STAGE)/lib/systemd/system/mic0-net.service
	install -d $(STAGE)/lib/udev/rules.d
	install -m644 90-mic0-net.rules $(STAGE)/lib/udev/rules.d/90-mic0-net.rules
	@if [ -z "$(DESTDIR)" ]; then depmod -a $(KERNEL_VERSION); fi
	@echo "已安装 mic.ko -> /lib/modules/$(KERNEL_VERSION)/"
	@echo "  配置：$(SYSCONFDIR)/modprobe.d/mic.conf、$(SYSCONFDIR)/udev/rules.d/50-udev-mic.rules"
	@echo "        $(SYSCONFDIR)/modules-load.d/mic.conf（开机自动 modprobe mic）"
	@echo "        $(STAGE)/lib/systemd/system/mic0-net.service + $(STAGE)/libexec/mpss/mic0-up.sh（mic0 出现即 up 并配地址）"
	@if [ -z "$(DESTDIR)" ]; then \
		systemctl daemon-reload 2>/dev/null || true; \
		udevadm control --reload-rules 2>/dev/null || true; \
		if systemctl enable mic0-net.service >/dev/null 2>&1; then \
			echo "        已 enable mic0-net.service"; \
			if [ -e /sys/class/net/mic0 ]; then \
				systemctl start mic0-net.service >/dev/null 2>&1 || true; \
				echo "        mic0 已在，当前：$$(ip -br addr show mic0 2>/dev/null | tr -s ' ')"; \
			else \
				echo "        mic0 尚未创建，等它出现时由设备单元自动拉起"; \
			fi; \
		else \
			echo "        （未能自动 enable，可手工 systemctl enable --now mic0-net.service）"; \
		fi; \
	fi
	@echo "        注：若用 NetworkManager 管 mic0，请先 systemctl disable mic0-net.service 以免互相覆盖"

clean:
	-$(MPSS_MAKE) MIC_CARD_ARCH=k1om KERNEL_SRC=$(KERNEL_SRC) clean
	-find . \( -name '*.o' -o -name '*.ko' -o -name '.*.cmd' -o -name '*.mod' -o -name '*.mod.c' \) -delete
	-rm -rf .tmp_versions .cache.mk
	-rm -f mic.ko
