# ============================================================================
# 构建辅助工具（随发布版提供，供其它包调用，本身无需安装）
#
#   gen-symver-map        从宏标注生成 ELF 版本脚本（上游是 Python 2，这里已移植到 Python 3）
#   gen-defsym.py         为 COI / MYO 生成公开 ABI 名的链接期别名汇编
#   metadata/             mpss-metadata.mk / mpss-metadata.c / .mpss-metadata
#                         （各包已各自带一份，这里留作参考与再分发）
# ============================================================================

PREFIX  ?= /usr
DESTDIR ?=
STAGE   := $(DESTDIR)$(PREFIX)

.PHONY: all build install clean
all: build

build:
	@echo "构建辅助工具（无需构建）："
	@ls -l gen-symver-map gen-defsym.py 2>/dev/null | sed 's/^/  /'
	@echo "  元数据：$(shell ls metadata 2>/dev/null | tr '\n' ' ')"
	@python3 -c "import sys; sys.exit(0 if sys.version_info[0] == 3 else 1)" \
		&& echo "  python3 可用（gen-symver-map 与 gen-defsym.py 需要 Python 3）"

install: build
	install -d $(STAGE)/bin
	install -m755 gen-symver-map $(STAGE)/bin/gen-symver-map
	install -m755 gen-defsym.py $(STAGE)/bin/gen-defsym.py
	@echo "已安装构建辅助工具 -> $(STAGE)/bin（可选）"

clean:
	@echo "无需清理"

uninstall:
	rm -f $(STAGE)/bin/gen-symver-map $(STAGE)/bin/gen-defsym.py
	-rmdir $(STAGE)/bin 2>/dev/null || true
	@echo "已卸载 00-build-tools"
