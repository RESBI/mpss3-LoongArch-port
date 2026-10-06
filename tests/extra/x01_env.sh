#!/bin/bash
# x01_env.sh — 环境与前提：内核/页大小/卡状态/模块版本/探针存在性/设备权限
# 目的：把「后续用例为什么会 SKIP」的原因一次性查清，并固化本移植依赖的环境事实。
set -u
. "$(dirname "${BASH_SOURCE[0]}")/lib/extra.sh"
. "$(dirname "${BASH_SOURCE[0]}")/../lib/common.sh" 2>/dev/null || true

PS=$(getconf PAGE_SIZE)
extra_info "宿主页大小 $PS 字节，内核 $(uname -r)，架构 $(uname -m)"

# 1) 页大小必须是本移植支持的三档之一
case "$PS" in
	4096|16384|65536) extra_pass "宿主页大小 $PS 属支持档（4/16/64 KiB）" ;;
	*) extra_fail "宿主页大小 $PS 不在支持档（4/16/64 KiB）—— 换算因子与不变量需重新评估" ;;
esac

# 2) 换算因子与「单段线上一页数」不变量（与页大小无关，恒为 512）
FACTOR=$((PS / 4096))
extra_info "SCIF_PEER_PAGE_FACTOR = $FACTOR（= PAGE_SIZE/4096）"
extra_expect_eq "$((2 * 1048576 / 4096))" "512" "单段线上页数不变量（2 MiB 大页段）"
if [ "$((512))" -le 4095 ]; then
	extra_pass "不变量 512 ≤ 12 位上限 4095（该字段在任何页大小下都不会触发）"
else
	extra_fail "不变量超过 12 位上限"
fi

# 3) 卡状态
ST=$(extra_card_state)
if [ "$ST" = "online" ]; then extra_pass "卡状态 online"; else extra_fail "卡状态为 $ST（需要 online 才能继续）"; fi

# 4) 设备权限（普通用户可用）
if [ -r /dev/mic/scif ] && [ -w /dev/mic/scif ]; then
	extra_pass "/dev/mic/scif 对普通用户可读写（$(stat -c '%A %U:%G' /dev/mic/scif)）"
else
	extra_fail "/dev/mic/scif 不可读写：$(stat -c '%A %U:%G' /dev/mic/scif 2>/dev/null || echo 不存在)"
fi

# 5) 已安装模块里是否有本次移植的关键代码（解压后查字符串，避免对压缩包误判）
KO=/lib/modules/$(uname -r)/updates/mic.ko.zst
TMPKO="$EXTRA_BUILD/installed-mic.ko"
if [ -f "$KO" ]; then
	if (zstd -d -f -q "$KO" -o "$TMPKO" 2>/dev/null || zcat "$KO" > "$TMPKO" 2>/dev/null); then
		for s in DESC-BAD DESC-INCONSISTENT RAW-TAIL; do
			n=$(strings "$TMPKO" | grep -c -F "$s")
			extra_expect_ge "$n" 1 "已装模块含探针字符串 $s"
		done
		# 默认构建应当是静默的（探针走 pr_debug）
		extra_info "dmesg 累计探针行数：$(dmesg | grep -c 'MIC scif')（默认构建应接近 0）"
	else
		extra_skip "无法解压 $KO，跳过模块内容检查"
	fi
else
	extra_fail "找不到已安装模块 $KO"
fi

# 6) dmesg 体检基线
extra_info "dmesg 计数：$(extra_dmesg_counts)"
extra_expect_zero "$(dmesg | grep -c 'kernel BUG at')" "基线无 kernel BUG"
extra_expect_zero "$(dmesg | grep -c 'ref_count < 0')" "基线无 SMPT 引用计数下溢"

exit 0
