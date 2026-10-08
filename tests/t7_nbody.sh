#!/bin/bash
# T7 重计算 + 大数据量：N 体引力模拟（卡端 O(N^2) 直接求和 + OpenMP）
#   覆盖：卡端自行生成初始条件、返回区回传、确定性校验和与宿主参考比对、能量守恒、GFLOPS
#   以普通用户运行（不需要 root）：bash tests/t7_nbody.sh
set -u
source "$(dirname "${BASH_SOURCE[0]}")/lib/common.sh"

PH=T7
head1 "T7 N 体引力模拟 offload（重计算 / 三档规模 / 校验和比对）"
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

# 1) 卡端 sink：必须 -rdynamic（导出符号）
#    现在由两个编译单元组成，**优化档刻意不同**：
#      nbody_vec.cpp  : -O2 -mavx512f —— 全部 FP64 向量 intrinsic 的 O(N^2) 热路径
#      nbody_sink.cpp : -O0          —— 含 sin/cos 与标量浮点的初始化/时序/校验和
#    为什么不能统一：-mavx512f 会把整个编译单元切到通用 AVX-512F，于是任何标量
#    浮点都变成 KNC 没有的 xmm 指令（docs/N2 约束 2）；而 -O2 的标量单元又会
#    生成带压缩位移的 vpackstorelpd（docs/N2 约束 3、约束 4）。
#    向量单元实测 xmm/ymm=0 且 vpackstore/vscatter=0，所以可以放心 -O2。
SINK="$LOGD/nbody_sink"
T7_OPT="${T7_OPT:--O0}"
k1om_cxx -O2 -mavx512f -fopenmp -c -I"$KSYS/usr/include" "$SRCS/nbody_vec.cpp" \
    -o "$LOGD/nbody_vec.o" >>"$LOGD/$PH.log" 2>&1
k1om_cxx $T7_OPT -fopenmp -rdynamic -c -I"$KSYS/usr/include" "$SRCS/nbody_sink.cpp" \
    -o "$LOGD/nbody_sink.o" >>"$LOGD/$PH.log" 2>&1
k1om_cxx -fopenmp -rdynamic "$LOGD/nbody_vec.o" "$LOGD/nbody_sink.o" \
    -L"$KSYS/usr/lib64" -lcoi_device -L"$COSLIB" -lgomp -lpthread -ldl -lrt \
    -Wl,-rpath,/tmp -o "$SINK" >>"$LOGD/$PH.log" 2>&1
if [ -f "$SINK" ]; then
  record PASS $PH "交叉编译卡端 nbody_sink（向量单元 -O2 -mavx512f + 标量单元 $T7_OPT）"
else
  record FAIL $PH "nbody_sink 编译失败（详见 $LOGD/$PH.log）"; summary; exit 1
fi

NSYM=$(readelf --dyn-syms "$SINK" 2>/dev/null | grep -ci nbodyrun)
[ "$NSYM" -ge 1 ] && record PASS $PH "sink 导出 NBodyRun（.dynsym $NSYM 条）" \
                  || record FAIL $PH "sink 未导出 NBodyRun（缺 -rdynamic？）"

# 优化档与指令统计（实测结论，详见 tests/README.md §五.2）：
#   卡端 sink 只能用 -O0 —— -O1/-O2 的产物在卡上立刻崩（segfault，ip 落在
#   `vpackstorelpd %zmm0,0x10(%r12){%k2}` 这条指令上）。
#   但**不能**拿「反汇编里有没有 vpackstore/vscatter」当判据：跑得通的 -O0 产物里
#   同样有 12 条（全部写栈），崩溃的 -O1/-O2 各有 11 条（其中一条改用寄存器寻址），
#   T5 那个 -O2、17/17 通过的 sink 也有 3 条。真正的判据是下面三档能不能跑通。
#   统计要用的 objdump：宿主 binutils 早已删除 k1om 支持，必须用 SDK 自带的那一个，
#   否则解析失败、grep 计数恒为 0（那是一次「假通过」）。
OBJ="$K1OM_OBJDUMP"
if has_objdump; then
  MASK=$("$OBJ" -d "$SINK" 2>/dev/null | grep -cE 'vpackstore|vscatter' || true)
  info "参考信息：sink 里 vpackstore/vscatter $MASK 条（-O0 实测 12 条且跑通，故条数不是判据）"
else
  info "参考信息：未找到 k1om objdump，跳过指令统计（设 K1OM_SDK 可提供）"
fi

# 2) 宿主
HOST="$LOGD/nbody_host"
g++ -O2 -fopenmp -I"$COI_INC" "$SRCS/nbody_host.cpp" -o "$HOST" \
    -L"$COI_LIB" -lcoi_host -Wl,-rpath,"$COI_LIB" >>"$LOGD/$PH.log" 2>&1
[ -f "$HOST" ] && record PASS $PH "编译宿主 nbody_host" || { record FAIL $PH "宿主编译失败"; summary; exit 1; }

# 3) 六档规模。前两档小规模，后四档是吞吐规模；档位 4/5/6 的工作集
#    已明显超出每核 L2（N=131072 时 7 个数组共 7 MB）。
for t in 1 2 3 4 5 6; do
  LOG="$LOGD/$PH-tier$t.log"
  LD_LIBRARY_PATH="$COI_LIB" timeout 600 "$HOST" "$SINK" "$COSLIB" "$t" >"$LOG" 2>&1
  RC=$?
  N=$(sed -n 's/.*N=\([0-9]*\).*/\1/p' "$LOG" | head -n 1)
  T=$(sed -n 's/.*卡上线程数[^:]*: *//p' "$LOG" | head -n 1 | tr -d ' ')
  E=$(sed -n 's/.*相对能量误差[^:]*: *//p' "$LOG" | head -n 1 | tr -d ' ')
  CS=$(sed -n 's/.*末态校验和[^:]*: *//p' "$LOG" | head -n 1 | tr -d ' ')
  GF=$(sed -n 's/.*估算算力[^:]*: *//p' "$LOG" | head -n 1 | tr -d ' ')
  MP=$(grep -a "粒子对速率" "$LOG" | head -n 1 | grep -aoE '[0-9]+\.[0-9]+' | head -n 1)
  info "档位 $t: N=$N 退出码=$RC 线程=$T 能量误差=$E 校验和=$CS $GF | ${MP:-?} MPairs/s"

  if [ "$RC" = "0" ] && grep -q "全部校验通过" "$LOG"; then
    record PASS $PH "档位 $t 全通过（N=$N，$T 线程，能量误差 $E，$GF | ${MP:-?} MPairs/s）"
  else
    record FAIL $PH "档位 $t 未通过（退出码 $RC）"
    tail -n 10 "$LOG" | sed 's/^/      /'
  fi

  # 宿主参考复算：小档默认做，大档默认跳过（O(N^2)·steps 太贵）。
  # 跳过记 SKIP 而不是 PASS——没验证过就不能算通过。T7_REF=1 可强制全部开启。
  if grep -q "已跳过" "$LOG"; then
    record SKIP $PH "档位 $t 宿主参考复算（N=$N 超默认阈值，T7_REF=1 可开启）"
  elif grep -q "与宿主参考一致" "$LOG"; then
    DIFF=$(sed -n 's/.*（差 \([^）]*\)）.*/\1/p' "$LOG" | head -n 1)
    record PASS $PH "档位 $t 校验和与宿主参考一致（N=$N，差 ${DIFF:-?}）"
  else
    record FAIL $PH "档位 $t 校验和与宿主参考不一致（N=$N）"
  fi
done

# rsqrt 精度自检：每一档都跑一次，全部必须达标
RSBAD=0
for t in 1 2 3 4 5 6; do
  LOG="$LOGD/$PH-tier$t.log"
  grep -q "rsqrt 精度达标" "$LOG" || RSBAD=$((RSBAD+1))
done
if [ "$RSBAD" = "0" ]; then
  RSV=$(grep -a "rsqrt 最大相对误差" "$LOGD/$PH-tier1.log" | head -n 1 | grep -aoE '[0-9]+\.[0-9]+e[-+][0-9]+' | head -n 1)
  record PASS $PH "卡上 rsqrt 精度达标（6/6 档，最大相对误差 ${RSV:-?} vs libm）"
else
  record FAIL $PH "卡上 rsqrt 精度不达标（$RSBAD/6 档失败）"
fi

# 受力核自检：端到端校验和测不到受力（实测把受力置零仍能通过），
# 所以这一项是唯一真正的受力定律验证，必须有。
ACBAD=0
for t in 1 2 3 4 5 6; do
  LOG="$LOGD/$PH-tier$t.log"
  grep -q "\[OK\] 受力核正确" "$LOG" || ACBAD=$((ACBAD+1))
done
if [ "$ACBAD" = "0" ]; then
  ACV=$(grep -a "受力核最大相对误差" "$LOGD/$PH-tier1.log" | head -n 1 | grep -aoE '[0-9]+\.[0-9]+e[-+][0-9]+' | head -n 1)
  record PASS $PH "卡上受力核正确（6/6 档，vs 标量 libm 参考，最大相对误差 ${ACV:-?}）"
else
  record FAIL $PH "卡上受力核不正确（$ACBAD/6 档失败）"
fi

UN=$(dmesg 2>/dev/null | grep -ci unaligned)
EX=$(dmesg 2>/dev/null | grep -cE 'Unable to handle|Oops|BUG: ')
[ "$UN" = "0" ] && record PASS $PH "无非对齐异常" || record FAIL $PH "非对齐异常 $UN 条"
[ "$EX" = "0" ] && record PASS $PH "无内核异常" || record FAIL $PH "内核异常 $EX 条"

summary
