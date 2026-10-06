#!/bin/bash
# T2 卡访问与部署：ssh 可达性、卡端环境、交叉编译、二进制部署（含 md5 校验）与执行。
#   用法：bash tests/t2_ssh.sh（普通用户即可，不需要 root）
set -u
source "$(dirname "${BASH_SOURCE[0]}")/lib/common.sh"

head1 "T2 卡访问与部署（ssh / 交叉编译 / 部署与执行）"
: >"$LOGD/T2.log"
need_card_host || { record SKIP $PH "卡地址未配置（设 CARD_HOST 或让 MPSS 配好 mic0.conf）"; summary; exit 0; }

# 1) ssh 可达
if card_up; then
  record PASS T2 "ssh 可达（$CARD）"
else
  record FAIL T2 "ssh 不可达（$CARD）——后续需要卡的功能测试会一并失败"
fi

# 2) 卡端环境
if card_up; then
  UP=$(card 'cut -d" " -f1 /proc/uptime' | tr -d '\r')
  KREL=$(card 'uname -r' | tr -d '\r')
  MEM=$(card 'grep -m1 MemTotal /proc/meminfo' | tr -d '\r')
  info "卡内核: ${KREL:-未知}   运行时长: ${UP:-?} 秒   ${MEM:-}"
  [ -n "$KREL" ] && record PASS T2 "读取卡端环境（内核 $KREL）" || record FAIL T2 "读取卡端环境失败"
  # 卡 rootfs 为 ramfs：重启即失，需每次重新部署（这是被测系统的固有特性）
  if card 'mount | grep -q " / " && mount | grep " / " | grep -q ramfs'; then
    record PASS T2 "确认卡 rootfs 为 ramfs（部署物重启后不保留，符合预期）"
  else
    record SKIP T2 "未识别卡 rootfs 类型"
  fi
fi

# 3) 交叉编译一个最小程序
SRC="$TESTS_DIR/src/hello_card.c"
mkdir -p "$TESTS_DIR/src"
if [ ! -f "$SRC" ]; then
  cat >"$SRC" <<'EOF'
/* 最小卡端程序：打印标识并返回 0，用于验证交叉编译与部署链路 */
#include <stdio.h>
int main(void) { printf("HELLO_FROM_CARD\n"); return 0; }
EOF
fi
BIN="$LOGD/hello_card"
if has_k1om_cc; then
  if k1om_cc -O2 -o "$BIN" "$SRC" >>"$LOGD/T2.log" 2>&1 && [ -f "$BIN" ]; then
    record PASS T2 "交叉编译卡端程序（$(stat -c %s "$BIN") 字节）"
  else
    record FAIL T2 "交叉编译失败（详见 $LOGD/T2.log）"
  fi
else
  record SKIP T2 "缺 k1om 交叉编译器：设 K1OM_SDK 或 K1OM_CC（当前：'${K1OM_CC:-未设}'）"
fi

# 4) 部署 + md5 校验 + 执行
if [ -f "$BIN" ] && card_up; then
  if deploy_card_file "$BIN" "/tmp/hello_card"; then
    record PASS T2 "部署到卡并校验 md5"
    OUT=$(card '/tmp/hello_card' | tr -d '\r')
    if [ "$OUT" = "HELLO_FROM_CARD" ]; then
      record PASS T2 "卡端执行输出正确（$OUT）"
    else
      record FAIL T2 "卡端执行输出异常：'$OUT'"
    fi
  else
    record FAIL T2 "部署或 md5 校验失败"
  fi
fi

# 5) 宿主侧的卡设备与 sysfs
chk() { local msg="$1"; shift; local f; for f in "$@"; do [ -e "$f" ] && { record PASS T2 "$msg"; return 0; }; done; record FAIL T2 "$msg（缺：$*）"; }
chk "存在 /dev/mic/scif"          /dev/mic/scif
chk "存在卡状态 sysfs"            /sys/class/mic/mic0/state
chk "存在卡网络接口"              /sys/class/net/mic0

summary
