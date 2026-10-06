#!/bin/bash
# T3 SCIF 消息通道：建链 + 小消息往返 + 大消息往返（26496，COI 创建命令尺寸）+ 反向消息 + fence。
#   用法：sudo bash tests/t3_scif_comm.sh
set -u
source "$(dirname "${BASH_SOURCE[0]}")/lib/common.sh"

PH=T3
head1 "T3 SCIF 消息通道（send/recv/fence，含 26496 字节大消息）"
: >"$LOGD/$PH.log"
need_card_host || { record SKIP $PH "卡地址未配置（设 CARD_HOST 或让 MPSS 配好 mic0.conf）"; summary; exit 0; }
need_scif      || { record SKIP $PH "缺用户态 SCIF 库（设 STAGE 或 COI_PREFIX）"; summary; exit 0; }

if ! scif_ok; then
  scif_hint
  record FAIL $PH "普通用户无法访问 $scif_dev —— 先执行 sudo bash tests/t1_install.sh 修好设备权限"
  summary; exit 1
fi
card_up || { record FAIL $PH "卡不可达，无法测试"; summary; exit 1; }

# 1) 构建
SRCS="$TESTS_DIR/src"
k1om_cc -O2 -o "$LOGD/sc_srv" "$SRCS/sc_srv.c" -lscif >>"$LOGD/$PH.log" 2>&1
[ -f "$LOGD/sc_srv" ] && record PASS $PH "交叉编译卡端 sc_srv" || record FAIL $PH "交叉编译 sc_srv 失败"
gcc -O2 -o "$LOGD/sc_cli" "$SRCS/sc_cli.c" -I"$SCIF_INC" -L"$SCIF_LIB" -lscif \
    -Wl,-rpath,"$SCIF_LIB" >>"$LOGD/$PH.log" 2>&1
[ -f "$LOGD/sc_cli" ] && record PASS $PH "编译宿主 sc_cli" || record FAIL $PH "编译 sc_cli 失败"

[ -f "$LOGD/sc_srv" ] && [ -f "$LOGD/sc_cli" ] || { summary; exit 1; }

# 2) 部署
deploy_card_file "$LOGD/sc_srv" '~/sc_srv' && record PASS $PH "部署卡端程序并校验 md5" \
  || { record FAIL $PH "部署卡端程序失败"; summary; exit 1; }

# 3) 运行：卡端后台，宿主前台
SRVLOG="$LOGD/sc_srv.log"; rm -f "$SRVLOG"
card 'pkill -x sc_srv 2>/dev/null; true' >/dev/null 2>&1
( timeout 120 ssh $SSHOPT "$CARD" '~/sc_srv 2>&1' >"$SRVLOG" 2>&1 ) &
sleep 4

CLILOG="$LOGD/sc_cli.log"
LD_LIBRARY_PATH="$COI_LIB" timeout 120 "$LOGD/sc_cli" >"$CLILOG" 2>&1
RC=$?
info "宿主退出码: $RC"
sed 's/^/    /' "$CLILOG"
sleep 4

# 4) 判定
head1 "T3 卡端输出"
grep -v 'WARNING\|post-quantum\|openssh.com/pq\|vulnerable' "$SRVLOG" 2>/dev/null | sed 's/^/    /'

judge() {  # judge <说明> <文件> <模式>
  if grep -q -- "$3" "$2" 2>/dev/null; then record PASS $PH "$1"; else record FAIL $PH "$1"; fi
}
judge "宿主小消息往返无误"   "$CLILOG" "小消息往返 64 字节，不符 0 个"
judge "宿主大消息往返无误"   "$CLILOG" "大消息往返 26496 字节，不符 0 个"
judge "宿主收到卡端主动消息" "$CLILOG" "卡端发来 seq="
judge "卡端小消息无误"       "$SRVLOG" "小消息收到 64 字节，不符 0 个"
judge "卡端大消息无误"       "$SRVLOG" "大消息收到 26496 字节，不符 0 个"
judge "卡端回显校验无误"     "$SRVLOG" "回显 seq="
judge "卡端走完 fence 并结束" "$SRVLOG" "SC_SRV: 完成"
judge "宿主走完 fence 并结束" "$CLILOG" "SC_CLI: 完成"

summary
