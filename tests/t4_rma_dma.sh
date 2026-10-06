#!/bin/bash
# T4 DMA/RMA：七档长度扫描（含偏移对照与非整页长度）+ 固定尺寸 26496 往返校验。
#   覆盖此前实测暴露的「按页步长算错导致只有第一页正确」缺陷。
#   以普通用户运行（不需要 root）：bash tests/t4_rma_dma.sh
set -u
source "$(dirname "${BASH_SOURCE[0]}")/lib/common.sh"

PH=T4
head1 "T4 DMA/RMA（writeto 七档长度 + 26496 全量校验）"
: >"$LOGD/$PH.log"
need_card_host || { record SKIP $PH "卡地址未配置（设 CARD_HOST 或让 MPSS 配好 mic0.conf）"; summary; exit 0; }
need_scif      || { record SKIP $PH "缺用户态 SCIF 库（设 STAGE 或 COI_PREFIX）"; summary; exit 0; }

if ! scif_ok; then
  scif_hint
  record FAIL $PH "普通用户无法访问 $scif_dev —— 先执行 sudo bash tests/t1_install.sh 修好设备权限"
  summary; exit 1
fi
card_up || { record FAIL $PH "卡不可达"; summary; exit 1; }

SRCS="$TESTS_DIR/src"

k1om_cc -O2 -o "$LOGD/sw_srv" "$SRCS/sw_srv.c" -lscif >>"$LOGD/$PH.log" 2>&1
[ -f "$LOGD/sw_srv" ] && record PASS $PH "交叉编译卡端 sw_srv" || record FAIL $PH "卡端编译失败"
gcc -O2 -o "$LOGD/sw_cli" "$SRCS/sw_cli.c" -I"$SCIF_INC" -L"$SCIF_LIB" -lscif \
    -Wl,-rpath,"$SCIF_LIB" >>"$LOGD/$PH.log" 2>&1
[ -f "$LOGD/sw_cli" ] && record PASS $PH "编译宿主 sw_cli" || record FAIL $PH "宿主编译失败"

k1om_cc -O2 -o "$LOGD/fw_srv" "$SRCS/fw_srv.c" -lscif >>"$LOGD/$PH.log" 2>&1
gcc -O2 -o "$LOGD/fw_cli" "$SRCS/fw_cli.c" -I"$SCIF_INC" -L"$SCIF_LIB" -lscif \
    -Wl,-rpath,"$SCIF_LIB" >>"$LOGD/$PH.log" 2>&1
[ -f "$LOGD/fw_srv" ] && [ -f "$LOGD/fw_cli" ] \
  && record PASS $PH "编译固定尺寸 26496 往返程序" || record FAIL $PH "固定尺寸程序编译失败"

[ -f "$LOGD/sw_srv" ] && [ -f "$LOGD/sw_cli" ] || { summary; exit 1; }

deploy_card_file "$LOGD/sw_srv" "~/sw_srv" && record PASS $PH "部署 sw_srv 并校验 md5" \
  || { record FAIL $PH "部署失败"; summary; exit 1; }
if [ -f "$LOGD/fw_srv" ]; then
  deploy_card_file "$LOGD/fw_srv" "~/fw_srv" \
    && record PASS $PH "部署 fw_srv 并校验 md5" || record FAIL $PH "部署 fw_srv 失败"
fi

SWLOG="$LOGD/sw_srv.log"; rm -f "$SWLOG"
card 'pkill -x sw_srv 2>/dev/null; true' >/dev/null 2>&1
( timeout 150 ssh $SSHOPT "$CARD" '~/sw_srv 2>&1' >"$SWLOG" 2>&1 ) &
sleep 4
CLILOG="$LOGD/sw_cli.log"
LD_LIBRARY_PATH="$COI_LIB" timeout 150 "$LOGD/sw_cli" >"$CLILOG" 2>&1
info "宿主退出码: $?"
sleep 10

head1 "T4 卡端逐例结果"
grep -v 'WARNING\|post-quantum\|openssh.com/pq\|vulnerable' "$SWLOG" 2>/dev/null | sed 's/^/    /'

for k in 0 1 2 3 4 5 6; do
  if grep -q "SW_SRV: case $k: .*0 bad" "$SWLOG" 2>/dev/null; then
    record PASS $PH "长度扫描第 $k 例逐字节正确"
  else
    record FAIL $PH "长度扫描第 $k 例不正确"
  fi
done

if [ -f "$LOGD/fw_srv" ]; then
  FWLOG="$LOGD/fw_srv.log"; rm -f "$FWLOG"
  card 'pkill -x fw_srv 2>/dev/null; true' >/dev/null 2>&1
  ( timeout 90 ssh $SSHOPT "$CARD" '~/fw_srv 2>&1' >"$FWLOG" 2>&1 ) &
  sleep 4
  FWCLI="$LOGD/fw_cli.log"
  LD_LIBRARY_PATH="$COI_LIB" timeout 90 "$LOGD/fw_cli" >"$FWCLI" 2>&1
  sleep 5
  if grep -q "26496 bytes, 0 mismatches" "$FWLOG" 2>/dev/null; then
    record PASS $PH "26496 字节 writeto 全量校验通过"
  else
    record FAIL $PH "26496 字节 writeto 校验未通过"
    grep -a 'FW_SRV' "$FWLOG" 2>/dev/null | tail -n 5 | sed 's/^/      /'
  fi
fi

UN=$(dmesg 2>/dev/null | grep -ci unaligned)
EX=$(dmesg 2>/dev/null | grep -cE 'Unable to handle|Oops|BUG: ')
[ "$UN" = "0" ] && record PASS $PH "无非对齐访问异常" || record FAIL $PH "出现 $UN 条非对齐异常"
[ "$EX" = "0" ] && record PASS $PH "无内核异常" || record FAIL $PH "出现 $EX 条内核异常"

summary
