#!/bin/bash
# x04_kernel_audit.sh — 内核日志审计与卡死体检（可在任何用例之后独立运行）
#   断言：无 kernel BUG、无 SMPT 引用计数下溢、无 Oops、无 DMA 通道超时风暴、无 D 状态进程
#   信息：DESC-* 探针计数（描述异常的痕迹）与探针行数（判断装的是不是静默构建）
set -u
. "$(dirname "${BASH_SOURCE[0]}")/lib/extra.sh"
. "$(dirname "${BASH_SOURCE[0]}")/../lib/common.sh" 2>/dev/null || true

extra_info "dmesg 计数：$(extra_dmesg_counts)"
extra_expect_zero "$(dmesg | grep -c 'kernel BUG at')" "无 kernel BUG"
extra_expect_zero "$(dmesg | grep -c 'ref_count < 0')" "无 SMPT 引用计数下溢"
extra_expect_zero "$(dmesg | grep -c 'Oops')" "无 Oops"

DTO=$(dmesg | grep -c 'request_dma_channel')
if [ "$DTO" -le 2 ]; then
	extra_pass "DMA 通道申请超时计数 $DTO（≤2 视为正常残余）"
else
	extra_fail "DMA 通道申请超时 $DTO 次 —— 疑似通道未释放"
fi

W=$(extra_wedged)
if [ "$W" = "no" ]; then extra_pass "无处于 D 状态的测试进程"; else extra_fail "有进程卡在内核态（D 状态）"; fi

ST=$(extra_card_state)
if [ "$ST" = "online" ]; then extra_pass "卡仍为 online"; else extra_fail "卡状态 $ST"; fi

PROBES=$(dmesg | grep -c 'MIC scif')
if [ "$PROBES" -le 50 ]; then
	extra_pass "探针行数 $PROBES（默认构建应为静默；>50 说明装的是 MIC_DEBUG=1 构建或异常路径被触发）"
else
	extra_info "探针行数 $PROBES —— 若是带诊断的构建则正常，否则检查是否有异常路径被触发"
fi
exit 0
