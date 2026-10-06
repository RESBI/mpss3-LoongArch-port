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

CFLAGS_MPSS := -g -O2 -fpermissive

build:
	$(SEARCH_ENV) $(MAKE) -f Makefile.mpss all prefix=$(PREFIX) CC=$(CC) CXX=$(CXX) \
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
	install -d $(STAGE)/lib/systemd/system
	install -m644 mpss.service $(STAGE)/lib/systemd/system/mpss.service
	@if [ -z "$(DESTDIR)" ]; then ldconfig; fi
	@echo "已安装 libmpssconfig / mpssd / micctrl（setuid）-> $(STAGE)"
	@echo "        systemd 单元 -> $(STAGE)/lib/systemd/system/mpss.service"
	@if [ -z "$(DESTDIR)" ]; then \
		systemctl daemon-reload 2>/dev/null || true; \
		if systemctl enable mpss >/dev/null 2>&1; then \
			echo "        已 enable mpss.service（开机自启）"; \
			if [ -d /sys/class/mic ]; then \
				systemctl restart mpss >/dev/null 2>&1 || true; \
				echo "        内核模块已在，服务状态：$$(systemctl is-active mpss)"; \
			else \
				echo "        内核模块尚未加载，加载后执行 systemctl start mpss"; \
			fi; \
		else \
			echo "        未能自动 enable，请手工 systemctl enable --now mpss"; \
		fi; \
	fi
	@echo "        注：早先手工放在 /etc/systemd/system/ 的同名单元会覆盖本文件"

clean:
	-$(MAKE) -f Makefile.mpss clean
	-find . -name '*.o' -delete
	-rm -f libmpssconfig/libmpssconfig.so* mpssd/mpssd micctrl/micctrl micctrl/micctrl_passwd

uninstall:
	@if [ -z "$(DESTDIR)" ]; then systemctl disable --now mpss 2>/dev/null || true; fi
	rm -f $(STAGE)/lib64/libmpssconfig.so.0.0.1 $(STAGE)/lib64/libmpssconfig.so.0 $(STAGE)/lib64/libmpssconfig.so
	rm -f $(STAGE)/include/mic/mpssconfig.h $(STAGE)/include/mic/libmpsscommon.h
	rm -f $(STAGE)/sbin/mpssd $(STAGE)/sbin/micctrl
	rm -f $(STAGE)/lib/systemd/system/mpss.service
	@if [ -z "$(DESTDIR)" ]; then systemctl daemon-reload 2>/dev/null || true; ldconfig; fi
	@echo "已卸载 libmpssconfig / mpssd / micctrl / mpss.service"
	@echo "  提示：/etc/systemd/system/mpss.service 为手工覆盖单元，未动，如需删除请自行处理"
