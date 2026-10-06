#!/bin/bash
# lib/extra.sh — 细粒度测试集的记录助手（由 run_extra.sh 与各用例 source）
#
# 记录格式固定为「PASS|FAIL|SKIP <用例> <说明>」，既便于人读也便于 runner 统计。

: "${EXTRA_LOGD:=/tmp}"
export EXTRA_RESULT="${EXTRA_RESULT:-$EXTRA_LOGD/.extra-result-$$}"

EXTRA_RESET() { : > "$EXTRA_RESULT" 2>/dev/null || true; }
extra_pass() { printf 'PASS %s\n' "$*" | tee -a "$EXTRA_RESULT"; }
extra_fail() { printf 'FAIL %s\n' "$*" | tee -a "$EXTRA_RESULT"; }
extra_skip() { printf 'SKIP %s\n' "$*" | tee -a "$EXTRA_RESULT"; }
extra_info() { printf '  · %s\n' "$*"; }

# 断言助手
extra_expect_eq() { # <实际> <期望> <说明>
	if [ "$1" = "$2" ]; then extra_pass "$3（=$1）"; else extra_fail "$3（实际 $1，期望 $2）"; fi
}
extra_expect_ge() { # <实际> <下限> <说明>
	if [ "$1" -ge "$2" ] 2>/dev/null; then extra_pass "$3（=$1 ≥ $2）"; else extra_fail "$3（实际 $1，期望 ≥ $2）"; fi
}
extra_expect_zero() { # <实际> <说明>
	if [ "$1" -eq 0 ] 2>/dev/null; then extra_pass "$2（=0）"; else extra_fail "$2（实际 $1，期望 0）"; fi
}

# dmesg 体检：打印各类计数（供各用例复用）
extra_dmesg_counts() {
	printf 'kernelBUG=%s refcount=%s oops=%s desc_bad=%s desc_inc=%s addr_not_found=%s dma_to=%s probes=%s' \
		"$(dmesg | grep -c 'kernel BUG at')" \
		"$(dmesg | grep -c 'ref_count < 0')" \
		"$(dmesg | grep -c 'Oops')" \
		"$(dmesg | grep -c 'DESC-BAD')" \
		"$(dmesg | grep -c 'DESC-INCONSISTENT')" \
		"$(dmesg | grep -c 'Addr not found')" \
		"$(dmesg | grep -c 'request_dma_channel')" \
		"$(dmesg | grep -c 'MIC scif')"
}

# 卡/进程状态
extra_card_state() { cat /sys/class/mic/mic0/state 2>/dev/null || echo unknown; }
extra_wedged() { P=$(pgrep -x bigxfer_cli 2>/dev/null | head -n1); [ -n "$P" ] && ps -o stat= -p "$P" | grep -q D && echo yes || echo no; }

# 编译探针（宿主 / 卡端），失败返回非 0
extra_build_probes() {
	local cc_ok=1
	if [ -n "${STAGE:-}" ] || [ -n "${COI_PREFIX:-}" ]; then
		gcc -O2 -o "$EXTRA_BUILD/probe_cli" "$EXTRA_SRCS/probe_cli.c" -lscif 2>"$EXTRA_LOGD/probe_cli.build.log" || cc_ok=0
	else
		cc_ok=0
	fi
	return $((1 - cc_ok))
}
extra_build_probe_srv() {
	need_k1om_cc >/dev/null 2>&1 || return 1
	k1om_cc -O2 "$EXTRA_SRCS/probe_srv.c" -lscif -o "$EXTRA_BUILD/probe_srv" 2>"$EXTRA_LOGD/probe_srv.build.log"
}
