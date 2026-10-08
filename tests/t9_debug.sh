#!/bin/bash
# T9 调试版本：只测档位 1
set -u
source "$(dirname "${BASH_SOURCE[0]}")/lib/common.sh"

PH=T9
LOGD="logs/run-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$LOGD"
SRCS="$TESTS_DIR/src"

echo "=== 编译 sink ==="
SINK="$LOGD/gemm_sink"
k1om_cxx -O0 -fopenmp -rdynamic -I"$KSYS/usr/include" "$SRCS/gemm_sink.cpp" \
    -L"$KSYS/usr/lib64" -lcoi_device -L"$COSLIB" -lgomp -lpthread -ldl -lrt \
    -Wl,-rpath,/tmp -o "$SINK" 2>&1 | tee "$LOGD/sink-compile.log"

if [ ! -f "$SINK" ]; then
    echo "✗ sink 编译失败"
    exit 1
fi
echo "✓ sink 编译成功"

echo ""
echo "=== 编译 host ==="
HOST="$LOGD/gemm_host"
g++ -O2 -I"$COI_INC" "$SRCS/gemm_host.cpp" -o "$HOST" \
    -L"$COI_LIB" -lcoi_host -Wl,-rpath,"$COI_LIB" 2>&1 | tee "$LOGD/host-compile.log"

if [ ! -f "$HOST" ]; then
    echo "✗ host 编译失败"
    exit 1
fi
echo "✓ host 编译成功"

echo ""
echo "=== 运行档位 1 ==="
LOG="$LOGD/T9-tier1.log"
LD_LIBRARY_PATH="$COI_LIB" timeout 120 "$HOST" "$SINK" "$COSLIB" 1 2>&1 | tee "$LOG"

echo ""
echo "=== 完成 ==="
echo "日志: $LOG"
