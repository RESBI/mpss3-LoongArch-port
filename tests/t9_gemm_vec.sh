#!/bin/bash
# T9-VEC：KNC 显式 IMCI intrinsics 向量化 GEMM
#   关键判据（不是"跑通就算"）：
#     1) 卡端 sink 必须 -mavx512f 编译，且反汇编里 vfmadd>0、xmm/ymm 出现次数=0
#        （-march=knc/-O2 的产物 vfmadd=0，即完全没有向量化——这正是旧版只有 6 GFLOPS 的原因）
#     2) 校验和与宿主 O(N^2) 参考一致，且宿主自查 O(N^3) 与 O(N^2) 两条路径互相吻合
set -u
source "$(dirname "${BASH_SOURCE[0]}")/lib/common.sh"

PH=T9V
head1 "T9-VEC KNC 显式向量化 GEMM（IMCI intrinsics）"
: >"$LOGD/$PH.log"

need_card_host  || { record SKIP $PH "卡地址未配置"; summary; exit 0; }
need_coi        || { record SKIP $PH "缺 COI 宿主库"; summary; exit 0; }
need_k1om_cxx   || { record SKIP $PH "缺 k1om 工具链"; summary; exit 0; }
need_sink_libs  || { record SKIP $PH "缺卡端依赖库目录"; summary; exit 0; }
need_gomp       || { record SKIP $PH "依赖库目录缺 libgomp"; summary; exit 0; }

if ! scif_ok; then
  scif_hint
  record FAIL $PH "普通用户无法访问 $scif_dev —— 先执行 sudo bash tests/t1_install.sh"
  summary; exit 1
fi
card_up || { record FAIL $PH "卡不可达"; summary; exit 1; }

SRCS="$TESTS_DIR/src"
SINK="$LOGD/gemm_vec_sink"
HOST="$LOGD/gemm_vec_host"
OBJ="$K1OM_OBJDUMP"

# 微内核尺寸默认走源文件里实测调好的值（MR32=8 MR64=4 JSB32=32 JSB64=16）；
# 只有显式设置环境变量时才覆盖，用于扫参数。
MRDEF=""
[ -n "${MR32:-}" ]  && MRDEF="$MRDEF -DMR32=$MR32"
[ -n "${MR64:-}" ]  && MRDEF="$MRDEF -DMR64=$MR64"
[ -n "${JSB32:-}" ] && MRDEF="$MRDEF -DJSB32=$JSB32"
[ -n "${JSB64:-}" ] && MRDEF="$MRDEF -DJSB64=$JSB64"
OPTFLAGS="${OPTFLAGS:--O2}"

# ---------------------------------------------------------------- 1) 编译卡端
k1om_cxx $OPTFLAGS -mavx512f -fopenmp -rdynamic -I"$KSYS/usr/include" \
    $MRDEF \
    "$SRCS/gemm_vec_sink.cpp" \
    -L"$KSYS/usr/lib64" -lcoi_device -L"$COSLIB" -lgomp -lpthread -ldl -lrt -lm \
    -Wl,-rpath,/tmp -o "$SINK" >>"$LOGD/$PH.log" 2>&1
if [ -f "$SINK" ]; then
    record PASS $PH "交叉编译卡端 gemm_vec_sink（$OPTFLAGS -mavx512f${MRDEF:-，用源文件默认尺寸}）"
else
    record FAIL $PH "gemm_vec_sink 编译失败（详见 $LOGD/$PH.log）"
    tail -n 20 "$LOGD/$PH.log" | sed 's/^/      /'
    summary; exit 1
fi

NSYM=$(readelf --dyn-syms "$SINK" 2>/dev/null | grep -ci gemmvecrun)
[ "$NSYM" -ge 1 ] && record PASS $PH "sink 导出 GemmVecRun（.dynsym $NSYM 条）" \
                  || record FAIL $PH "sink 未导出 GemmVecRun（缺 -rdynamic？）"

# ------------------------------------------- 2) 指令级验收：确认真的向量化了
if has_objdump; then
    VFMA=$("$OBJ" -d "$SINK" 2>/dev/null | grep -c 'vfmadd')
    VZMM=$("$OBJ" -d "$SINK" 2>/dev/null | grep -c 'zmm')
    VXY=$("$OBJ"  -d "$SINK" 2>/dev/null | grep -cE '%[xy]mm')
    VTOT=$("$OBJ" -d "$SINK" 2>/dev/null | grep -cE '^\s+[0-9a-f]+:')
    info "指令统计：总 $VTOT 条，zmm $VZMM，vfmadd $VFMA，xmm/ymm $VXY"
    [ "$VFMA" -gt 0 ] && record PASS $PH "内层循环已向量化（vfmadd $VFMA 条）" \
                      || record FAIL $PH "未生成任何 vfmadd —— 说明又退回标量代码"
    [ "$VXY" -eq 0 ] && record PASS $PH "无非法的 xmm/ymm 指令（KNC 只有 512-bit 单元）" \
                     || record FAIL $PH "产物含 $VXY 条 xmm/ymm 指令，卡上会崩"
else
    info "未找到 k1om objdump，跳过指令级验收（设 K1OM_SDK 可提供）"
fi

# ---------------------------------------------------------------- 3) 编译宿主
g++ -O2 -I"$COI_INC" "$SRCS/gemm_vec_host.cpp" -o "$HOST" \
    -L"$COI_LIB" -lcoi_host -Wl,-rpath,"$COI_LIB" >>"$LOGD/$PH.log" 2>&1
[ -f "$HOST" ] && record PASS $PH "编译宿主 gemm_vec_host" \
               || { record FAIL $PH "宿主编译失败"; tail -n 20 "$LOGD/$PH.log" | sed 's/^/      /'; summary; exit 1; }

# ---------------------------------------------------------------- 4) 分档运行
TIERS="${TIERS:-256 1024 2048}"
for N in $TIERS; do
    for TH in ${THREADS:-240}; do
        LOG="$LOGD/$PH-N${N}-T${TH}.log"
        LD_LIBRARY_PATH="$COI_LIB" timeout 900 "$HOST" "$SINK" "$COSLIB" "$N" "$TH" 2 >"$LOG" 2>&1
        RC=$?
        GF32=$(sed -n 's/.*\[FP32\].*//p' "$LOG" | head -n1)
        F32=$(grep -A2 '\[FP32\]' "$LOG" | sed -n 's/.*卡端算力 *: *\([0-9.]*\).*/\1/p' | head -n1)
        F64=$(grep -A6 '\[FP64\]' "$LOG" | sed -n 's/.*卡端算力 *: *\([0-9.]*\).*/\1/p' | head -n1)
        N32=$(grep -c '✓ 通过' "$LOG")
        info "N=$N 线程=$TH 退出码=$RC FP32=${F32:-?}GFLOPS FP64=${F64:-?}GFLOPS 通过项=$N32"
        if [ "$RC" = "0" ] && [ "$N32" = "2" ]; then
            record PASS $PH "N=$N 线程=$TH（FP32 ${F32} GFLOPS / FP64 ${F64} GFLOPS）"
        else
            record FAIL $PH "N=$N 线程=$TH 未通过（退出码 $RC）"
            tail -n 14 "$LOG" | sed 's/^/      /'
        fi
    done
done

summary
