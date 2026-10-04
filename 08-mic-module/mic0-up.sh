#!/bin/sh
# ============================================================================
# 把主机侧 mic0 网口 up 起来并按 /etc/mpss/mic0.conf 配上地址。
#
# 为什么要这个脚本：mic0 这个网口由 mic.ko 的 vnet 部分在内核里创建（卡上 virtio-net
# 设备出现时自动建），但**创建 ≠ 配置** —— 把网口 up、给它配 IP 从来不是驱动的事。
# MPSS 侧对应的是 mic0.conf 里的 modhost= 选项，但它只会去改 Debian 的
# /etc/network/interfaces 或 Red Hat 的 ifcfg-* 文件，AOSC 这类发行版没有那些文件，
# 所以我们把地址直接取自同一份 mic0.conf，自己配，保持单一配置来源。
#
# 提前退出（不视为失败）的情况：配置文件缺失、没有 Network 行、没有 hostip。
# 用法：
#   mic0-up.sh                 正常执行
#   mic0-up.sh --dry-run       只打印将要执行的命令
#   MIC0_DEV=mic0 MIC0_WAIT=60 mic0-up.sh
# ============================================================================
set -u

CONF=${MPSS_CONFIG_FILE:-/etc/mpss/mic0.conf}
DEV=${MIC0_DEV:-mic0}
WAIT=${MIC0_WAIT:-45}
DRY=0
[ "${1:-}" = "--dry-run" ] && DRY=1

if [ ! -f "$CONF" ]; then
	echo "mic0-up: 找不到 $CONF（先跑 micctrl --initdefaults），不做任何事"
	exit 0
fi

line=$(grep -m1 '^Network' "$CONF" 2>/dev/null || true)
if [ -z "$line" ]; then
	echo "mic0-up: $CONF 里没有 Network 行，不做任何事"
	exit 0
fi

hostip=$(printf '%s\n' "$line" | sed -n 's/.*hostip=\([^ ]*\).*/\1/p')
netbits=$(printf '%s\n' "$line" | sed -n 's/.*netbits=\([^ ]*\).*/\1/p')
mtu=$(printf '%s\n' "$line" | sed -n 's/.*mtu=\([^ ]*\).*/\1/p')
netbits=${netbits:-24}

if [ -z "$hostip" ]; then
	echo "mic0-up: Network 行里没有 hostip=，不做任何事"
	exit 0
fi

echo "mic0-up: 配置 $DEV 为 ${hostip}/${netbits}${mtu:+（MTU $mtu）}，取自 $CONF"

if [ "$DRY" = "1" ]; then
	echo "mic0-up: --dry-run，将执行："
	echo "  ip link set $DEV mtu ${mtu:-<不动>}"
	echo "  ip addr replace ${hostip}/${netbits} dev $DEV"
	echo "  ip link set $DEV up"
	exit 0
fi

# 等网口出现（模块刚加载、卡刚引导完时都可能还没建好）
i=0
while [ ! -e "/sys/class/net/$DEV" ] && [ "$i" -lt "$WAIT" ]; do
	sleep 1
	i=$((i + 1))
done
if [ ! -e "/sys/class/net/$DEV" ]; then
	echo "mic0-up: 等了 ${WAIT} 秒仍没有 $DEV（mic.ko 没加载？卡没引导？），本次不做"
	exit 0
fi

if [ -n "$mtu" ]; then ip link set "$DEV" mtu "$mtu" || true; fi
ip addr replace "${hostip}/${netbits}" dev "$DEV"
ip link set "$DEV" up
ip -br addr show "$DEV"
