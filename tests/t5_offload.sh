#!/bin/bash
# T5 COI Offload 端到端：sink 符号导出校验 → 创建进程 → 取函数句柄 → 卡上 OpenMP 计算 → 结果校验。
#   覆盖此前实测暴露的三处缺陷：创建命令传输、对端窗口长度、sink 缺 -rdynamic。
#   以普通用户运行（不需要 root）：bash tests/t5_offload.sh
set -u
source "$(dirname "${BASH_SOURCE[0]}")/lib/common.sh"

PH=T5
head1 "T5 COI Offload 端到端（含 sink 动态符号与结果正确性校验）"
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
N="${TEST_N:-200000000}"

# 1) 卡端 sink —— 必须带 -rdynamic，否则 CardReduce 不在 .dynsym，卡端 dlsym 会失败
SINK="$LOGD/offload_sink"
k1om_cxx -O2 -fopenmp -rdynamic -I"$KSYS/usr/include" "$SRCS/offload_sink.cpp" \
    -L"$KSYS/usr/lib64" -lcoi_device -L"$COSLIB" -lgomp -lpthread -ldl -lrt \
    -Wl,-rpath,/tmp -o "$SINK" >>"$LOGD/$PH.log" 2>&1
if [ -f "$SINK" ]; then
  record PASS $PH "交叉编译卡端 sink"
else
  record FAIL $PH "sink 编译失败（详见 $LOGD/$PH.log）"; summary; exit 1
fi

NSYM=$(readelf --dyn-syms "$SINK" 2>/dev/null | grep -ci cardreduce)
if [ "$NSYM" -ge 1 ]; then
  record PASS $PH "sink 导出 CardReduce（.dynsym $NSYM 条）"
else
  record FAIL $PH "sink 未导出 CardReduce —— 卡端 dlsym 必然失败"
fi

# 2) 宿主程序
HOST="$LOGD/offload_host"
g++ -O2 -I"$COI_INC" "$SRCS/offload_host.cpp" -o "$HOST" \
    -L"$COI_LIB" -lcoi_host -Wl,-rpath,"$COI_LIB" >>"$LOGD/$PH.log" 2>&1
[ -f "$HOST" ] && record PASS $PH "编译宿主 offload 程序" || { record FAIL $PH "宿主编译失败"; summary; exit 1; }

# 3) 运行
DP0=$(card 'pidof coi_daemon' | tr -d '\r')
[ -n "$DP0" ] && record PASS $PH "运行前卡端 daemon 在（pid=$DP0）" || record FAIL $PH "运行前 daemon 不在"

OUTLOG="$LOGD/$PH-run.log"
LD_LIBRARY_PATH="$COI_LIB" timeout 300 "$HOST" "$SINK" "$N" "$COSLIB" >"$OUTLOG" 2>&1
RC=$?
head1 "T5 运行输出"
sed 's/^/    /' "$OUTLOG"

judge() { if grep -q -- "$2" "$OUTLOG" 2>/dev/null; then record PASS $PH "$1"; else record FAIL $PH "$1"; fi; }
[ "$RC" = "0" ] && record PASS $PH "退出码 0" || record FAIL $PH "退出码 $RC"
judge "枚举到引擎"       "1) 引擎数 = 1"
judge "取到引擎句柄"     "2) 取到引擎 0 句柄"
judge "卡上创建进程成功" "3) 在卡上创建进程 -> COI_SUCCESS(0)"
judge "建立管道成功"     "4) 建立管道"
judge "取到卡端函数句柄" "5) 取到卡端函数句柄: CardReduce"
judge "卡上计算完成"     "7) 卡上计算完成"
judge "端到端收尾完成"   "offload 端到端完成"
judge "参与线程数为 240" "实际参与线程数     : 240"

ERR=$(sed -n 's/.*相对误差[^:]*: *//p' "$OUTLOG" | head -n 1 | tr -d ' ')
if [ -n "$ERR" ]; then
  if awk -v e="$ERR" 'BEGIN{ if (e<0) e=-e; exit !(e<1e-12) }'; then
    record PASS $PH "归约结果与解析值一致（相对误差 $ERR）"
  else
    record FAIL $PH "归约结果偏差过大（相对误差 $ERR）"
  fi
else
  record FAIL $PH "未能从输出中解析相对误差"
fi

DP1=$(card 'pidof coi_daemon' | tr -d '\r')
[ -n "$DP1" ] && record PASS $PH "运行后 daemon 仍在（pid=$DP1）" || record FAIL $PH "运行后 daemon 退出"
UN=$(dmesg 2>/dev/null | grep -ci unaligned)
EX=$(dmesg 2>/dev/null | grep -cE 'Unable to handle|Oops|BUG: ')
[ "$UN" = "0" ] && record PASS $PH "无非对齐异常" || record FAIL $PH "非对齐异常 $UN 条"
[ "$EX" = "0" ] && record PASS $PH "无内核异常" || record FAIL $PH "内核异常 $EX 条"

summary
