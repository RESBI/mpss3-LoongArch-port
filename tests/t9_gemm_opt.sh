#!/usr/bin/env bash
# T9 GEMM 优化版性能测试：对比 -O0 标量版 vs -O2 -march=knc 自动向量化版

set -e
TESTS_DIR="$(cd "$(dirname "$0")" && pwd)"
source "$TESTS_DIR/lib/common.sh"

head1 "T9 GEMM 性能对比测试"

need_card_host
need_coi
need_k1om_cxx
need_sink_libs
need_gomp

SRCS="$TESTS_DIR/src"

# 编译标量版（-O0，强制重新编译）
k1om_cxx -O0 -fopenmp -rdynamic \
    -I"$KSYS/usr/include" \
    "$SRCS/gemm_sink_simple.cpp" \
    -L"$KSYS/usr/lib64" -lcoi_device \
    -L"$COSLIB" -lgomp -lpthread -ldl -lrt -lm \
    -Wl,-rpath,/tmp \
    -o "$LOGD/gemm_sink_scalar" >>"$LOGD/T9.log" 2>&1 || { record FAIL T9 "编译标量版失败"; summary; exit 1; }
record PASS T9 "标量版 sink (-O0)"

# 编译优化版（-O2 -march=knc）
k1om_cxx -march=knc -O2 -fopenmp -rdynamic \
    -I"$KSYS/usr/include" \
    "$SRCS/gemm_sink_opt.cpp" \
    -L"$KSYS/usr/lib64" -lcoi_device \
    -L"$COSLIB" -lgomp -lpthread -ldl -lrt -lm \
    -Wl,-rpath,/tmp \
    -o "$LOGD/gemm_sink_opt" || record FAIL T9 "编译优化版失败"
record PASS T9 "优化版 sink (-O2 -march=knc)"

# 编译宿主端（通用）
g++ -O2 -I"$COI_INC" \
    "$SRCS/gemm_host.cpp" \
    -o "$LOGD/gemm_host" \
    -L"$COI_LIB" -lcoi_host \
    -Wl,-rpath,"$COI_LIB" || record FAIL T9 "宿主编译失败"
record PASS T9 "宿主编译"

# 测试函数
run_gemm_test() {
    local N=$1
    local threads=$2
    local sink_bin=$3
    local label=$4
    local tier_log="$LOGD/T9-${label}-N${N}.log"
    
    # 根据 N 确定档位
    local tier=1
    if [ "$N" = "1024" ]; then tier=2; fi
    if [ "$N" = "2048" ]; then tier=3; fi
    
    echo "T9 GEMM ${label}: N=${N}, threads=${threads}" > "$tier_log"
    echo "" >> "$tier_log"
    
    LD_LIBRARY_PATH="$COI_LIB" "$LOGD/gemm_host" "$sink_bin" "$COSLIB" "$tier" >> "$tier_log" 2>&1
    local ret=$?
    
    if [ $ret -eq 0 ]; then
        # 提取 FP32 和 FP64 的 GFLOPS 值
        local fp32_gflops=$(grep "卡端算力:" "$tier_log" | head -1 | awk '{print $2}')
        local fp64_gflops=$(grep "卡端算力:" "$tier_log" | tail -1 | awk '{print $2}')
        echo "  ${label}: N=${N} FP32=${fp32_gflops} FP64=${fp64_gflops}"
        record PASS T9 "${label} N=${N} (FP32 ${fp32_gflops}, FP64 ${fp64_gflops})"
    else
        record FAIL T9 "${label} N=${N} 失败"
    fi
}

# 对比测试：三档负载，两个版本
for N in 256 1024 2048; do
    echo ""
    echo "====== N=${N} 性能对比 ======"
    run_gemm_test $N 60 "$LOGD/gemm_sink_scalar" "标量版"
    run_gemm_test $N 60 "$LOGD/gemm_sink_opt" "优化版"
done

summary
