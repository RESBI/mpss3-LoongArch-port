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

BOOTDIR := $(DESTDIR)$(PREFIX)/share/mpss/boot

build:
	@echo "无需构建：这两个镜像是从 MPSS 3.8.6 交付里取出、并按龙芯主机需要改过的"
	@ls -l bzImage-knightscorner initramfs-knightscorner.cpio.gz

install: build
	install -d $(BOOTDIR)
	install -m644 bzImage-knightscorner $(BOOTDIR)/bzImage-knightscorner
	install -m644 initramfs-knightscorner.cpio.gz $(BOOTDIR)/initramfs-knightscorner.cpio.gz
	@[ -f $(BOOTDIR)/System.map-knightscorner ] || : > $(BOOTDIR)/System.map-knightscorner
	@echo "已安装卡端引导镜像 -> $(BOOTDIR)"
	@echo "注：System.map 是占位空文件（没有卡内核的符号表），mpssd 会打一条 mmap 告警，不影响引导"

clean:
	@echo "无需清理"

uninstall:
	rm -f $(BOOTDIR)/bzImage-knightscorner $(BOOTDIR)/initramfs-knightscorner.cpio.gz
	-rmdir $(BOOTDIR) 2>/dev/null || true
	@echo "已卸载启动镜像（$(BOOTDIR)）"
