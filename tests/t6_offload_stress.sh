#!/bin/bash
# T6 较高压力 offload：多轮、大计算规模、重复创建/销毁进程，检查稳定性与资源回收。
#   以普通用户运行（不需要 root）：bash tests/t6_offload_stress.sh
#   规模可用 TEST_N / TEST_N2 / TEST_N3 / TEST_ROUNDS 覆盖。
set -u
source "$(dirname "${BASH_SOURCE[0]}")/lib/common.sh"

PH=T6
head1 "T6 较高压力 offload（多轮 + 大规模 + 反复创建销毁）"
: >"$LOGD/$PH.log"
need_card_host || { record SKIP $PH "卡地址未配置（设 CARD_HOST 或让 MPSS 配好 mic0.conf）"; summary; exit 0; }
need_coi       || { record SKIP $PH "缺 COI 宿主库（设 STAGE 或 COI_PREFIX）"; summary; exit 0; }
need_k1om_cxx  || { record SKIP $PH "缺 k1om 工具链（设 K1OM_SDK 或 K1OM_CXX）"; summary; exit 0; }
need_sink_libs || { record SKIP $PH "缺卡端依赖库目录（设 SINK_LIBS）"; summary; exit 0; }
need_gomp      || { record SKIP $PH "依赖库目录缺 libgomp（设 SINK_LIBS 指向含卡端 libgomp 的目录）"; summary; exit 0; }

if ! scif_ok; then
  scif_hint
  record FAIL $PH "普通用户无法访问 $scif_dev —— 先执行 sudo bash tests/t1_install.sh 修好设备权限"
  summary; exit 1
fi
card_up || { record FAIL $PH "卡不可达"; summary; exit 1; }

SRCS="$TESTS_DIR/src"

SINK="$LOGD/offload_sink"; HOST="$LOGD/offload_host"
if [ ! -f "$SINK" ]; then
  k1om_cxx -O2 -fopenmp -rdynamic -I"$KSYS/usr/include" "$SRCS/offload_sink.cpp" \
      -L"$KSYS/usr/lib64" -lcoi_device -L"$COSLIB" -lgomp -lpthread -ldl -lrt \
      -Wl,-rpath,/tmp -o "$SINK" >>"$LOGD/$PH.log" 2>&1
fi
if [ ! -f "$HOST" ]; then
  g++ -O2 -I"$COI_INC" "$SRCS/offload_host.cpp" -o "$HOST" \
      -L"$COI_LIB" -lcoi_host -Wl,-rpath,"$COI_LIB" >>"$LOGD/$PH.log" 2>&1
fi
[ -f "$SINK" ] && [ -f "$HOST" ] || { record FAIL $PH "缺少 offload 产物（请先跑 t5）"; summary; exit 1; }
record PASS $PH "复用/构建 offload 产物（sink $(stat -c %s "$SINK") 字节）"

ROUNDS="${TEST_ROUNDS:-3}"
DP0=$(card 'pidof coi_daemon' | tr -d '\r')
record PASS $PH "起始 daemon pid=${DP0:-无}"

for r in $(seq 1 "$ROUNDS"); do
  case "$r" in
    1) N="${TEST_N:-200000000}";;
    2) N="${TEST_N2:-2000000000}";;
    *) N="${TEST_N3:-500000000}";;
  esac
  LOG="$LOGD/$PH-round$r.log"
  START=$(date +%s)
  LD_LIBRARY_PATH="$COI_LIB" timeout 600 "$HOST" "$SINK" "$N" "$COSLIB" >"$LOG" 2>&1
  RC=$?
  ELAPSED=$(( $(date +%s) - START ))
  CARDT=$(sed -n 's/.*卡上耗时[^:]*: *//p' "$LOG" | head -n 1 | tr -d ' ')
  SUM=$(sed -n 's/.*归约结果 sum[^:]*: *//p' "$LOG" | head -n 1 | tr -d ' ')
  ERR=$(sed -n 's/.*相对误差[^:]*: *//p' "$LOG" | head -n 1 | tr -d ' ')

  info "第 $r 轮: n=$N 退出码=$RC 总耗时=${ELAPSED}s 卡上=${CARDT:-?}s sum=${SUM:-?} 误差=${ERR:-?}"
  if [ "$RC" = "0" ] && grep -q "端到端完成" "$LOG"; then
    record PASS $PH "第 $r 轮 n=$N 完成（卡上 ${CARDT:-?}s，误差 ${ERR:-?}）"
  else
    record FAIL $PH "第 $r 轮 n=$N 未完成（退出码 $RC）"
    tail -n 8 "$LOG" | sed 's/^/      /'
  fi

  DPN=$(card 'pidof coi_daemon' | tr -d '\r')
  [ -n "$DPN" ] && record PASS $PH "第 $r 轮后 daemon 仍在（pid=$DPN）" \
                || record FAIL $PH "第 $r 轮后 daemon 退出"
  UN=$(dmesg 2>/dev/null | grep -ci unaligned)
  EX=$(dmesg 2>/dev/null | grep -cE 'Unable to handle|Oops|BUG: ')
  [ "$UN" = "0" ] && [ "$EX" = "0" ] && record PASS $PH "第 $r 轮后内核无异常" \
    || record FAIL $PH "第 $r 轮后内核异常（unaligned=$UN, 异常=$EX）"
done

LOG="$LOGD/$PH-final.log"
LD_LIBRARY_PATH="$COI_LIB" timeout 300 "$HOST" "$SINK" "${TEST_NFINAL:-50000000}" "$COSLIB" >"$LOG" 2>&1
grep -q "端到端完成" "$LOG" && record PASS $PH "压力后仍可正常 offload" || record FAIL $PH "压力后无法再 offload"

head1 "T6 汇总"
info "轮数: $ROUNDS"

summary
