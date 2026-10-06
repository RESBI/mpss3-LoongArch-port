#!/bin/bash
# 安装（阶段 T1）：把编译产物安装到系统（make install）并加载/启动，使卡进入 online。
#   用法：sudo bash tests/t1_install.sh
# 本脚本只安装与启动，不编译（编译见 tests/t0_build.sh）、不测试（功能测试见 tests/run_tests.sh）。
set -u
source "$(dirname "${BASH_SOURCE[0]}")/lib/common.sh"

head1 "T1 安装（make install → depmod → modprobe → 启动 mpss）"
if ! need_root "安装到 /usr 与 /lib/modules"; then
  record SKIP T1 "未以 root 运行，跳过安装"
  summary; exit $?
fi

: >"$LOGD/T1.log"
need_release_tree || { record SKIP T1 "缺发布树（设 RELEASE_DIR）"; summary; exit 0; }
cd "$RELEASE_DIR" || { record FAIL T1 "无法进入发布树"; summary; exit 1; }

FAILED=""
for p in 00-build-tools 02-libscif 03-mpss-daemon 04-mpss-micmgmt 05-miccheck \
         06-mpss-coi 07-mpss-myo 08-mic-module 09-boot-images; do
  [ -d "$p" ] || continue
  printf '  --- 安装 %s ...\n' "$p"
  if make -C "$p" install PREFIX="$PREFIX" >>"$LOGD/T1.log" 2>&1; then
    record PASS T1 "安装 $p"
  else
    record FAIL T1 "安装 $p（详见 $LOGD/T1.log）"
    FAILED="$FAILED $p"
  fi
done

head1 "T1 关键安装位置检查"
chk() { local msg="$1"; shift; local f; for f in "$@"; do [ -e "$f" ] && { record PASS T1 "$msg"; return 0; }; done; record FAIL T1 "$msg（缺：$*）"; }
chk "libscif.so 已安装"      "$PREFIX/lib64/libscif.so" "$PREFIX/lib/libscif.so"
chk "mpssd 已安装"           "$PREFIX/sbin/mpssd" "$PREFIX/bin/mpssd"
chk "mic 模块已安装"         "$KODIR/mic.ko" "$KODIR/mic.ko.zst"
chk "卡启动镜像已安装"       "$PREFIX/share/mpss/boot/initramfs-knightscorner.cpio.gz"
chk "COI 宿主库已安装"       "$PREFIX/lib64/libcoi_host.so" "$PREFIX/lib/libcoi_host.so"

head1 "T1 加载模块并启动 MPSS 栈"
if lsmod | grep -q '^mic '; then
  record PASS T1 "mic 模块已在载（跳过重复加载）"
else
  depmod -a >>"$LOGD/T1.log" 2>&1
  if modprobe mic >>"$LOGD/T1.log" 2>&1 && lsmod | grep -q '^mic '; then
    record PASS T1 "modprobe mic"
  else
    record FAIL T1 "modprobe mic（详见 $LOGD/T1.log）"
  fi
fi

head1 "T1 设备权限（使普通用户可用，使用场景不应需要 root）"
if [ -e /dev/mic/scif ]; then
  # 上游 50-udev-mic.rules 用旧式 NAME="mic/%k"，systemd-udev 忽略该写法，
  # MODE="0666" 落不到节点上，设备因此退回 0600 root:root。这里补一条现代规则
  # （与随包安装的 08-mic-module/55-mic-perms.rules 同内容）；不改上游那份文件 ——
  # 规则按文件名顺序生效，55 排在 50 之后。随包已装过则跳过。
  if [ ! -e /etc/udev/rules.d/55-mic-perms.rules ] && [ ! -e /usr/lib/udev/rules.d/55-mic-perms.rules ]; then
    cat >/etc/udev/rules.d/55-mic-perms.rules <<'EOF'
# MIC SCIF / ctrl：允许普通用户访问。
# 注意：udev 算出的节点名是 /dev/scif，真实节点却在 /dev/mic/scif，MODE= 会落空，
# 因此用 RUN+ 直接对真实节点 chmod（两条路径都试）。
ACTION=="add|change", SUBSYSTEM=="mic", KERNEL=="scif", MODE="0666", RUN+="/bin/sh -c 'chmod 666 /dev/mic/scif /dev/scif 2>/dev/null; chmod o+x /dev/mic 2>/dev/null; true'"
ACTION=="add|change", SUBSYSTEM=="mic", KERNEL=="ctrl", MODE="0666", RUN+="/bin/sh -c 'chmod 666 /dev/mic/ctrl /dev/ctrl 2>/dev/null; chmod o+x /dev/mic 2>/dev/null; true'"
EOF
  fi
  udevadm control --reload-rules >>"$LOGD/T1.log" 2>&1
  udevadm trigger --action=add /sys/class/mic/scif >>"$LOGD/T1.log" 2>&1
  udevadm trigger --action=add /sys/class/mic/ctrl >>"$LOGD/T1.log" 2>&1
  chmod 666 /dev/mic/scif /dev/mic/ctrl 2>/dev/null
  chmod o+x /dev/mic 2>/dev/null
  MODE=$(stat -c %a /dev/mic/scif 2>/dev/null)
  if [ "$MODE" = "666" ]; then
    record PASS T1 "设备权限已放开（/dev/mic/scif 模式 $MODE）"
  else
    record FAIL T1 "设备权限未生效（模式 $MODE）"
  fi
  # 以普通用户实际打开设备作为验收判据
  if su -s /bin/bash -c 'exec 3<>/dev/mic/scif' "$REAL_USER" >/dev/null 2>&1; then
    record PASS T1 "普通用户（$REAL_USER）可访问 scif 设备 —— 使用场景无需 root ✓"
  else
    record FAIL T1 "普通用户（$REAL_USER）仍无法访问 scif 设备"
  fi
else
  record FAIL T1 "设备节点 /dev/mic/scif 不存在（模块是否已加载？）"
fi

head1 "T1 交付：目录属主交回调用者（便于普通用户跑测试）"
if [ -n "${REAL_USER:-}" ]; then
  chown -R "$REAL_USER" "$TESTS_DIR" 2>/dev/null \
    && record PASS T1 "测试目录属主 → $REAL_USER（含历史产物，避免普通用户无法覆盖）" \
    || record SKIP T1 "测试目录属主未调整（不阻断）"
fi

systemctl start mpss >>"$LOGD/T1.log" 2>&1
# 卡进入 online 需要卡侧网络就绪：本环境默认不自动启用该单元，这里显式拉起（best-effort）
if [ -e /etc/systemd/system/mic0-net.service ] || systemctl list-unit-files 2>/dev/null | grep -q '^mic0-net'; then
  systemctl start mic0-net >>"$LOGD/T1.log" 2>&1 \
    && record PASS T1 "启动卡侧网络单元 mic0-net" \
    || record SKIP T1 "mic0-net 启动未成功（不阻断，继续等 online）"
fi
if wait_online 300; then
  record PASS T1 "卡进入 online（状态=$(cat /sys/class/mic/mic0/state 2>/dev/null)）"
else
  record FAIL T1 "卡未在 300 秒内 online（当前: $(cat /sys/class/mic/mic0/state 2>/dev/null)）"
fi

head1 "T1 卡端服务检查"
DP=$(wait_daemon 120 || true)
if [ -n "${DP:-}" ]; then
  record PASS T1 "卡端 coi_daemon 运行中（pid=$DP）"
else
  record FAIL T1 "卡端 coi_daemon 未启动"
fi

summary
