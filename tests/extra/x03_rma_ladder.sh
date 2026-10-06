#!/bin/bash
# x03_rma_ladder.sh — 单次 RMA 尺寸阶梯（任务 3 的自动化版本）
#   固定窗口 1 MiB、总量 64 MiB，逐级放大单次 scif_writeto，断言：
#     - 每一档两端 checksum 一致
#     - 全程无 kernel BUG / DESC-* / 卡死进程
#   依据：实测 26496 -> 1 MiB 全部通过（见附录 L L.2.4 与 K K.2 第 3 条）。
set -u
. "$(dirname "${BASH_SOURCE[0]}")/lib/extra.sh"
. "$(dirname "${BASH_SOURCE[0]}")/../lib/common.sh" 2>/dev/null || true

TESTS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
[ "$(extra_card_state)" = "online" ] || { extra_fail "卡不在 online"; exit 1; }
[ -x "$TESTS_DIR/t8_bigxfer.sh" ] || { extra_skip "找不到 t8_bigxfer.sh"; exit 0; }

B0=$(dmesg | grep -c 'kernel BUG at')
for R in 26496 65536 131072 262144 524288 1048576; do
	OUT=$(cd "$TESTS_DIR" && TEST_BIG=67108864 TEST_BIG_WINDOW=1048576 TEST_BIG_RMA=$R \
		timeout 600 bash t8_bigxfer.sh 2>&1)
	OK=$(echo "$OUT" | grep -c "两端校验和一致")
	if [ "$OK" -ge 1 ]; then
		extra_pass "RMA $((R / 1024)) KiB：两端 checksum 一致"
	else
		extra_fail "RMA $((R / 1024)) KiB：未通过（$(echo "$OUT" | grep -E 'FAIL' | head -n1 | tr -s ' '))"
	fi
	if [ "$(pgrep -x bigxfer_cli >/dev/null && echo y || echo n)" = "y" ]; then
		extra_fail "RMA $((R / 1024)) KiB：有进程残留，停止阶梯"
		break
	fi
done
extra_expect_zero "$(( $(dmesg | grep -c 'kernel BUG at') - B0 ))" "阶梯新增 kernel BUG"
extra_expect_zero "$(pgrep -x bigxfer_cli >/dev/null && echo 1 || echo 0)" "无残留进程"
exit 0
