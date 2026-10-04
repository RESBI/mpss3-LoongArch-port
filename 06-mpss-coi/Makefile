# ============================================================================
# MPSS 3.8.6 主机侧工具端 · LoongArch 移植版 · 发布版包装 Makefile
#
#   make            # 只构建（产出 build/libcoi_host.so）
#   make install    # 构建并安装（默认 PREFIX=/usr，需要 root）
#   make install DESTDIR=/tmp/stage    # 装到暂存目录，不需要 root
#   make clean      # 清理构建产物
#
# 依赖：先装 02-libscif（本包需要 -lscif 与 scif.h）。用 DESTDIR 暂存时，本 Makefile
# 会自动把 $(DESTDIR)$(PREFIX) 加进头文件与库搜索路径，所以按顺序暂存安装也能通过。
#
# 关于 COI 的公开 ABI 名：上游用 __asm__(".symver X1,X@@COI_1.0") 建立，而被包含该头的
# 每个目标文件只定义其中几个符号，新 binutils 拒绝这种写法。移植版在非 x86 上不发射那段
# 汇编（补丁 9），改为链接期用 --defsym 建立别名（见 00-build-tools/gen-defsym.py），
# 版本节点仍由包内 coi_version_linker_script.map 指定。
# ============================================================================

PREFIX  ?= /usr
DESTDIR ?=
CC      ?= gcc
CXX     ?= g++
STAGE   := $(DESTDIR)$(PREFIX)
RELROOT := $(CURDIR)/..
TOOLS   := $(RELROOT)/00-build-tools

# 上游链接这棵树时用的基础参数（取自 Makefile.mpss 在 WHAT=HOST 下的展开结果）
BASE_LD := -pthread -ldl -lrt -Wl,-z,relro,-z,now -lscif

.PHONY: all build install clean
all: build

build:
	python3 $(TOOLS)/gen-defsym.py --pkg coi --mode defsym --out build/coi_defsym.args
	LIBRARY_PATH=$(STAGE)/lib64:$(PREFIX)/lib64 \
	C_INCLUDE_PATH=$(STAGE)/include CPLUS_INCLUDE_PATH=$(STAGE)/include \
	$(MAKE) -f Makefile.mpss WHAT=HOST build/libcoi_host.so \
		LD_FLAGS="$(BASE_LD) $$(cat build/coi_defsym.args)"
	ln -sf libcoi_host.so build/libcoi_host.so.0
	@echo "产出：build/libcoi_host.so（$$(nm -D --defined-only build/libcoi_host.so | wc -l) 个动态符号）"

install: build
	install -d $(STAGE)/lib64 \
		$(STAGE)/include/intel-coi/common $(STAGE)/include/intel-coi/sink \
		$(STAGE)/include/intel-coi/source $(STAGE)/include/intel-coi/internal
	install -m755 build/libcoi_host.so $(STAGE)/lib64/libcoi_host.so.0
	ln -sf libcoi_host.so.0 $(STAGE)/lib64/libcoi_host.so
	install -m644 src/include/common/*   $(STAGE)/include/intel-coi/common/
	install -m644 src/include/sink/*     $(STAGE)/include/intel-coi/sink/
	install -m644 src/include/source/*   $(STAGE)/include/intel-coi/source/
	install -m644 src/include/internal/* $(STAGE)/include/intel-coi/internal/
	@echo "已安装 libcoi_host.so -> $(STAGE)/lib64，头文件 -> $(STAGE)/include/intel-coi/"
	@echo "用法：示例里写作 #include <intel-coi/source/COIEngine_source.h>，编译时 -lcoi_host"

clean:
	-$(MAKE) -f Makefile.mpss clean
	-rm -rf build
