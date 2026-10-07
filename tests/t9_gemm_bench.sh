#!/bin/bash
# T9 GEMM 等算子浮点性能基准测试
#   - 三档负载：256×256 / 1024×1024 / 2048×2048
#   - FP32 与 FP64 分别测试
#   - 输出 GFLOPS、验证正确性
#
# 依赖：COI + k1om 工具链 + 卡端 libgomp
# 用法：bash tests/t9_gemm_bench.sh
set -u
source "$(dirname "${BASH_SOURCE[0]}")/lib/common.sh"

PH=T9
head1 "T9 GEMM 等算子浮点性能基准测试"
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

# 1) 卡端 sink：与 T7 相同，-O0 避免 -O2 的崩溃问题
SINK="$LOGD/gemm_sink"
k1om_cxx -O0 -fopenmp -rdynamic -I"$KSYS/usr/include" "$SRCS/gemm_sink.cpp" \
    -L"$KSYS/usr/lib64" -lcoi_device -L"$COSLIB" -lgomp -lpthread -ldl -lrt \
    -Wl,-rpath,/tmp -o "$SINK" >>"$LOGD/$PH.log" 2>&1
if [ -f "$SINK" ]; then
  record PASS $PH "交叉编译卡端 gemm_sink"
else
  record FAIL $PH "gemm_sink 编译失败（详见 $LOGD/$PH.log）"; summary; exit 1
fi

# 检查符号导出
NSYM_FP32=$(readelf --dyn-syms "$SINK" 2>/dev/null | grep -ci gemm_fp32)
NSYM_FP64=$(readelf --dyn-syms "$SINK" 2>/dev/null | grep -ci gemm_fp64)
if [ "$NSYM_FP32" -ge 1 ] && [ "$NSYM_FP64" -ge 1 ]; then
  record PASS $PH "sink 导出 gemm_fp32 与 gemm_fp64 符号"
else
  record FAIL $PH "sink 未导出必要符号（gemm_fp32=$NSYM_FP32, gemm_fp64=$NSYM_FP64，缺 -rdynamic？）"
fi

# 2) 宿主
HOST="$LOGD/gemm_host"
g++ -O2 -I"$COI_INC" "$SRCS/gemm_host.cpp" -o "$HOST" \
    -L"$COI_LIB" -lcoi_host -Wl,-rpath,"$COI_LIB" >>"$LOGD/$PH.log" 2>&1
[ -f "$HOST" ] && record PASS $PH "编译宿主 gemm_host" || { record FAIL $PH "宿主编译失败"; summary; exit 1; }

# 3) 三档规模
for t in 1 2 3; do
  LOG="$LOGD/$PH-tier$t.log"
  LD_LIBRARY_PATH="$COI_LIB" timeout 600 "$HOST" "$SINK" "$COSLIB" "$t" >"$LOG" 2>&1
  RC=$?
  
  # 解析输出
  N=$(sed -n 's/.*N=\([0-9]*\).*/\1/p' "$LOG" | head -n 1)
  GF_FP32=$(sed -n '/\[FP32/,/通过\|失败/{s/.*卡端算力:[[:space:]]*\([0-9.]*\)[[:space:]]*GFLOPS.*/\1/p}' "$LOG" | head -n 1)
  GF_FP64=$(sed -n '/\[FP64/,/通过\|失败/{s/.*卡端算力:[[:space:]]*\([0-9.]*\)[[:space:]]*GFLOPS.*/\1/p}' "$LOG" | head -n 1)
  PASS_FP32=$(grep -c "✓ FP32 通过" "$LOG" || true)
  PASS_FP64=$(grep -c "✓ FP64 通过" "$LOG" || true)
  
  info "档位 $t: N=$N 退出码=$RC FP32=${GF_FP32}GFLOPS FP64=${GF_FP64}GFLOPS"

  if [ "$RC" = "0" ] && [ "$PASS_FP32" = "1" ] && [ "$PASS_FP64" = "1" ]; then
    record PASS $PH "档位 $t 全通过（N=$N，FP32 ${GF_FP32} GFLOPS，FP64 ${GF_FP64} GFLOPS）"
  else
    record FAIL $PH "档位 $t 未通过（退出码 $RC，FP32通过=$PASS_FP32，FP64通过=$PASS_FP64）"
    tail -n 20 "$LOG" | sed 's/^/      /'
  fi
done

# 内核异常检查
UN=$(dmesg 2>/dev/null | grep -ci unaligned)
EX=$(dmesg 2>/dev/null | grep -cE 'Unable to handle|Oops|BUG: ')
[ "$UN" = "0" ] && record PASS $PH "无非对齐异常" || record FAIL $PH "非对齐异常 $UN 条"
[ "$EX" = "0" ] && record PASS $PH "无内核异常" || record FAIL $PH "内核异常 $EX 条"

summary
