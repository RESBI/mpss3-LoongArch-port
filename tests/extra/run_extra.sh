#!/bin/bash
# run_extra.sh — 细粒度功能测试集（与 run_tests.sh 分开，互不依赖）
#
#   bash tests/extra/run_extra.sh            # 跑全部用例
#   bash tests/extra/run_extra.sh --list     # 列出用例
#   bash tests/extra/run_extra.sh -o x05     # 只跑名字含 x05 的用例
#   bash tests/extra/run_extra.sh -q         # 只打印统计
#
# 用例约定（每个 tests/extra/xNN_*.sh）：
#   - 自己 source ../lib/common.sh 取公共助手
#   - 用 extra_pass/extra_fail/extra_skip 记录结论（见 lib/extra.sh）
#   - 退出码 0 表示该用例无失败项
set -u

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TESTS_DIR="$(cd "$HERE/.." && pwd)"
LOGD="$HERE/logs/$(date +%Y%m%d-%H%M%S)"
mkdir -p "$LOGD"
export EXTRA_LOGD="$LOGD"
export EXTRA_SRCS="$HERE/src"
export EXTRA_BUILD="$HERE/build"
mkdir -p "$EXTRA_BUILD"

QUIET=0
ONLY=""
while [ $# -gt 0 ]; do
	case "$1" in
		--list) ls "$HERE"/x*.sh 2>/dev/null | xargs -n1 basename | sed 's/\.sh$//' ; exit 0 ;;
		-o|--only) ONLY="$2"; shift ;;
		-q|--quiet) QUIET=1 ;;
		-h|--help) sed -n '2,12p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
		*) echo "未知参数: $1"; exit 2 ;;
	esac
	shift
done

. "$HERE/lib/extra.sh"

echo "===== 细粒度功能测试集 ====="
echo "  用例目录: $HERE"
echo "  日志目录: $LOGD"
echo "  宿主: $(uname -m) 内核 $(uname -r) 页大小 $(getconf PAGE_SIZE)"
echo "  卡状态: $(cat /sys/class/mic/mic0/state 2>/dev/null || echo 未知)"
echo

TOTAL_P=0; TOTAL_F=0; TOTAL_S=0; N_CASE=0; N_CASE_FAIL=0
FAILED_CASES=""

for c in "$HERE"/x*.sh; do
	[ -f "$c" ] || continue
	name="$(basename "$c" .sh)"
	case "$name" in
		*"$ONLY"*) ;;
		*) continue ;;
	esac
	N_CASE=$((N_CASE + 1))
	[ "$QUIET" = "0" ] && echo "--- $name ---"
	EXTRA_RESET
	out="$(cd "$TESTS_DIR" && EXTRA_LOGD="$LOGD" EXTRA_SRCS="$EXTRA_SRCS" EXTRA_BUILD="$EXTRA_BUILD" \
		bash "$c" 2>&1)"
	rc=$?
	echo "$out" > "$LOGD/$name.log"
	printf '%s\n' "$out" | grep -E "^ *(PASS|FAIL|SKIP)" | sed 's/^/    /' | { [ "$QUIET" = "0" ] && cat || cat >/dev/null; }
	# 统计来自 lib/extra.sh 写入的结果文件
	P=$(grep -c '^PASS' "$EXTRA_RESULT" 2>/dev/null | head -n1); P=${P:-0}
	F=$(grep -c '^FAIL' "$EXTRA_RESULT" 2>/dev/null | head -n1); F=${F:-0}
	S=$(grep -c '^SKIP' "$EXTRA_RESULT" 2>/dev/null | head -n1); S=${S:-0}
	TOTAL_P=$((TOTAL_P + P)); TOTAL_F=$((TOTAL_F + F)); TOTAL_S=$((TOTAL_S + S))
	if [ "$F" -gt 0 ] || [ "$rc" -ne 0 ]; then
		N_CASE_FAIL=$((N_CASE_FAIL + 1))
		FAILED_CASES="$FAILED_CASES $name"
	fi
	[ "$QUIET" = "0" ] && printf '    小计: 通过 %s 失败 %s 跳过 %s（退出码 %s）\n' "$P" "$F" "$S" "$rc"
done

echo
echo "===== 汇总 ====="
echo "  用例 $N_CASE 个（失败用例 $N_CASE_FAIL 个${FAILED_CASES:+：$FAILED_CASES}）"
echo "  检查项 通过 $TOTAL_P  失败 $TOTAL_F  跳过 $TOTAL_S"
echo "  日志: $LOGD"
[ "$TOTAL_F" -eq 0 ] && echo "  结论: 全部通过" || echo "  结论: 有失败项"
exit $([ "$TOTAL_F" -eq 0 ] && echo 0 || echo 1)
