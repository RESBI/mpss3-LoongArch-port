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

# 卡上实测到的版本号，写进 _miccheck/common/version.py，供自检比对
MPSS_FLASH_VERSION ?= 391
SMC_FW_VERSION     ?= 1.17.6900

build:
	$(MAKE) -f Makefile.mpss version MPSS_FLASH_VERSION=$(MPSS_FLASH_VERSION) \
		SMC_FW_VERSION=$(SMC_FW_VERSION)
	@echo "生成 _miccheck/common/version.py："
	@cat _miccheck/common/version.py

install: build
	install -d $(STAGE)/bin $(STAGE)/src/miccheck
	install -m755 bin/miccheck.py $(STAGE)/bin/miccheck
	sed -i '1s|.*|#!/usr/bin/env python3|' $(STAGE)/bin/miccheck
	rm -rf $(STAGE)/src/miccheck/_miccheck
	cp -r _miccheck $(STAGE)/src/miccheck/
	@echo "已安装 miccheck -> $(STAGE)/bin/miccheck（模块在 $(STAGE)/src/miccheck）"
	@echo "用法：PYTHONPATH=$(PREFIX)/src/miccheck python3 $(PREFIX)/bin/miccheck"

clean:
	-$(MAKE) -f Makefile.mpss clean
	-find . -name '__pycache__' -type d -prune -exec rm -rf {} +
	-rm -f _miccheck/common/version.py
