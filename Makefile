# 发布版顶层便利 Makefile：按依赖顺序依次构建安装各包。
# 单包安装请进各自目录执行 make install（这是本发布版的主用法）。
PREFIX  ?= /usr
DESTDIR ?=
PKGS := 00-build-tools 02-libscif 03-mpss-daemon 04-mpss-micmgmt 05-miccheck \
        06-mpss-coi 07-mpss-myo 08-mic-module 09-boot-images

.PHONY: all install clean $(PKGS)

all:
	@for p in $(PKGS); do echo "=== build $$p ==="; $(MAKE) -C $$p build PREFIX=$(PREFIX) DESTDIR=$(DESTDIR) || exit 1; done

install:
	@for p in $(PKGS); do echo "=== install $$p ==="; $(MAKE) -C $$p install PREFIX=$(PREFIX) DESTDIR=$(DESTDIR) || exit 1; done

clean:
	@for p in $(PKGS); do $(MAKE) -C $$p clean || true; done
