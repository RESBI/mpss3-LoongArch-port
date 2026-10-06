#!/bin/bash
# x02_window_direction.sh — 窗口尺寸的方向性验证（宿主方向）
#   流程：交叉编译 probe_srv -> 投送到卡端 -> 启动 -> 宿主 probe_cli window <size> -> 汇总结论
#   目的：验证「宿主注册的窗口不受卡端 512 段限制」这一代码读法。
set -u
. "$(dirname "${BASH_SOURCE[0]}")/lib/extra.sh"
. "$(dirname "${BASH_SOURCE[0]}")/../lib/common.sh" 2>/dev/null || true

PS=$(getconf PAGE_SIZE)
if ! need_k1om_cc >/dev/null 2>&1; then extra_skip "缺 k1om 工具链"; exit 0; fi
if ! need_scif >/dev/null 2>&1; then extra_skip "缺用户态 SCIF 库"; exit 0; fi
[ "$(extra_card_state)" = "online" ] || { extra_fail "卡不在 online"; exit 1; }

extra_build_probe_srv || { extra_fail "probe_srv 交叉编译失败（见 $EXTRA_LOGD/probe_srv.build.log）"; exit 1; }
extra_build_probes    || { extra_fail "probe_cli 编译失败（见 $EXTRA_LOGD/probe_cli.build.log）"; exit 1; }
extra_pass "探针编译成功（宿主与卡端）"

card "pkill -x probe_srv" >/dev/null 2>&1
deploy_card_file "$EXTRA_BUILD/probe_srv" /tmp/probe_srv >/dev/null 2>&1 || { extra_fail "投送 probe_srv 失败"; exit 1; }
card "nohup /tmp/probe_srv 4194304 > /tmp/probe_srv.log 2>&1 &" >/dev/null 2>&1
sleep 3
LISTEN=$(card "grep -c 'listening' /tmp/probe_srv.log" 2>/dev/null | tr -dc 0-9)
extra_expect_ge "${LISTEN:-0}" 1 "卡端 probe_srv 已启动监听"

for MB in 1 4 8 16; do
	SZ=$((MB * 1048576))
	OUT=$("$EXTRA_BUILD/probe_cli" window "$SZ" 2>&1)
	if echo "$OUT" | grep -q "注册成功"; then
		extra_pass "宿主方向注册 ${MB} MiB 成功（段数上限参考：$((SZ / PS)) 段 @ ${PS} 字节页）"
	else
		extra_fail "宿主方向注册 ${MB} MiB 失败：$(echo "$OUT" | grep -E '失败' | head -n1)"
	fi
done

card "pkill -x probe_srv" >/dev/null 2>&1
extra_expect_zero "$(dmesg | grep -c 'kernel BUG at')" "窗口注册未引发 kernel BUG"
extra_expect_zero "$(dmesg | grep -c 'ref_count < 0')" "窗口注册未引发 SMPT 计数下溢"
exit 0
