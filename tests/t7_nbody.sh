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

# 1) 卡端 sink：必须 -rdynamic（导出符号），且只能用 -O0（-O1/-O2 的产物在卡上必崩，见下）
SINK="$LOGD/nbody_sink"
k1om_cxx -O0 -fopenmp -rdynamic -I"$KSYS/usr/include" "$SRCS/nbody_sink.cpp" \
    -L"$KSYS/usr/lib64" -lcoi_device -L"$COSLIB" -lgomp -lpthread -ldl -lrt \
    -Wl,-rpath,/tmp -o "$SINK" >>"$LOGD/$PH.log" 2>&1
if [ -f "$SINK" ]; then
  record PASS $PH "交叉编译卡端 nbody_sink"
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

# 3) 三档规模
for t in 1 2 3; do
  LOG="$LOGD/$PH-tier$t.log"
  LD_LIBRARY_PATH="$COI_LIB" timeout 600 "$HOST" "$SINK" "$COSLIB" "$t" >"$LOG" 2>&1
  RC=$?
  N=$(sed -n 's/.*N=\([0-9]*\).*/\1/p' "$LOG" | head -n 1)
  T=$(sed -n 's/.*卡上线程数[^:]*: *//p' "$LOG" | head -n 1 | tr -d ' ')
  E=$(sed -n 's/.*相对能量误差[^:]*: *//p' "$LOG" | head -n 1 | tr -d ' ')
  CS=$(sed -n 's/.*末态校验和[^:]*: *//p' "$LOG" | head -n 1 | tr -d ' ')
  GF=$(sed -n 's/.*估算算力[^:]*: *//p' "$LOG" | head -n 1 | tr -d ' ')
  info "档位 $t: N=$N 退出码=$RC 线程=$T 能量误差=$E 校验和=$CS $GF"

  if [ "$RC" = "0" ] && grep -q "全部校验通过" "$LOG"; then
    record PASS $PH "档位 $t 全通过（N=$N，$T 线程，能量误差 $E，$GF）"
  else
    record FAIL $PH "档位 $t 未通过（退出码 $RC）"
    tail -n 10 "$LOG" | sed 's/^/      /'
  fi

  # 小规模档必须与宿主参考实现逐位一致
  if [ "$t" = "1" ]; then
    if grep -q "与宿主参考一致" "$LOG"; then
      DIFF=$(sed -n 's/.*（差 \([^）]*\)）.*/\1/p' "$LOG" | head -n 1)
      record PASS $PH "档位 1 校验和与宿主参考一致（差 ${DIFF:-?}）"
    else
      record FAIL $PH "档位 1 校验和与宿主参考不一致"
    fi
  fi
done

UN=$(dmesg 2>/dev/null | grep -ci unaligned)
EX=$(dmesg 2>/dev/null | grep -cE 'Unable to handle|Oops|BUG: ')
[ "$UN" = "0" ] && record PASS $PH "无非对齐异常" || record FAIL $PH "非对齐异常 $UN 条"
[ "$EX" = "0" ] && record PASS $PH "无内核异常" || record FAIL $PH "内核异常 $EX 条"

summary
