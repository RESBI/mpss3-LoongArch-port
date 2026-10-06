# 发布版顶层便利 Makefile：按依赖顺序依次构建安装各包。
# 单包安装请进各自目录执行 make install（这是本发布版的主用法）。
#
# 顺序说明：04-mpss-micmgmt 编译时需要 08-mic-module 的开发头文件
#   （mic/io_interface.h、mic/micras_api.h 等，由 08 的 dev_install 装到 $(PREFIX)/include/mic；
#    04 经 EXTRA_INC=$(PREFIX)/include → CPLUS_INCLUDE_PATH 找到它们）。
#   因此 PKGS 把 08-mic-module 排在 04-mpss-micmgmt 之前；但 build 不产生这些头文件，
#   全新环境（或刚执行过 make uninstall）必须先跑一次：
#       sudo make -C 08-mic-module install
PREFIX  ?= /usr
DESTDIR ?=
PKGS := 00-build-tools 02-libscif 03-mpss-daemon 08-mic-module 04-mpss-micmgmt 05-miccheck \
        06-mpss-coi 07-mpss-myo 09-boot-images

.PHONY: all install clean $(PKGS)

all:
	@for p in $(PKGS); do echo "=== build $$p ==="; $(MAKE) -C $$p build PREFIX=$(PREFIX) DESTDIR=$(DESTDIR) || exit 1; done

install:
	@for p in $(PKGS); do echo "=== install $$p ==="; $(MAKE) -C $$p install PREFIX=$(PREFIX) DESTDIR=$(DESTDIR) || exit 1; done

clean:
	@for p in $(PKGS); do $(MAKE) -C $$p clean || true; done

# 卸载：进入各子项目执行各自的 uninstall（PREFIX/DESTDIR 原样透传）
uninstall:
	@for p in $(PKGS); do echo "=== uninstall $$p ==="; \
		$(MAKE) -C $$p uninstall PREFIX=$(PREFIX) DESTDIR=$(DESTDIR) || exit 1; done
	@echo "各子项目卸载完成；/etc/mic、日志与用户自建文件未动"
