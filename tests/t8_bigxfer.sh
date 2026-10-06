#!/bin/bash
# T8 大数据量传输：把一大块数据（默认 4 GiB）送到卡上，两端各自 checksum 并比对，同时测算传输带宽。
#   覆盖：卡端大块 malloc（逐页 touch，OOM 当场暴露）、卡端最大可注册窗口探测、
#         分块 RMA（scif_writeto）、位置敏感校验和、传输耗时与带宽
#   以普通用户运行（不需要 root）：bash tests/t8_bigxfer.sh
#   规模可覆盖：TEST_BIG（默认 4294967296 = 4 GiB）、TEST_BIG_WINDOW（默认 67108864 = 64 MiB）；
#   run_tests.sh --quick 会把 TEST_BIG 降到 256 MiB。
set -u
source "$(dirname "${BASH_SOURCE[0]}")/lib/common.sh"

PH=T8
head1 "T8 大数据量传输（默认 4 GiB 到卡上 / 两端 checksum / 带宽）"
: >"$LOGD/$PH.log"
need_card_host || { record SKIP $PH "卡地址未配置（设 CARD_HOST 或让 MPSS 配好 mic0.conf）"; summary; exit 0; }
need_scif      || { record SKIP $PH "缺用户态 SCIF 库（设 STAGE 或 COI_PREFIX）"; summary; exit 0; }
need_k1om_cc   || { record SKIP $PH "缺 k1om C 编译器（设 K1OM_SDK 或 K1OM_CC）"; summary; exit 0; }

if ! scif_ok; then
  scif_hint
  record FAIL $PH "普通用户无法访问 $scif_dev —— 先执行 sudo bash tests/t1_install.sh 修好设备权限"
  summary; exit 1
fi
card_up || { record FAIL $PH "卡不可达"; summary; exit 1; }

BIG="${TEST_BIG:-4294967296}"                 # 4 GiB
WIN="${TEST_BIG_WINDOW:-1048576}"             # 注册窗口 1 MiB（T4 用 256 KiB，故 1 MiB 仍是保守值）
RMA="${TEST_BIG_RMA:-26496}"                  # 单次 scif_writeto 的字节数：取 T4 已验证的 26496
# 注册与协议页都要求 4096 的整数倍；校验和按 8 字节字处理，4096 的倍数自然满足
if [ $((BIG % 65536)) -ne 0 ] || [ $((WIN % 4096)) -ne 0 ] || [ "$BIG" -le 0 ] || [ "$WIN" -le 0 ]; then
  record FAIL $PH "规模参数不合法：TEST_BIG=$BIG TEST_BIG_WINDOW=$WIN（BIG 须为 64 KiB 倍数，WIN 须为 4 KiB 倍数）"
  summary; exit 1
fi
if [ "$RMA" -le 0 ] || [ "$RMA" -gt "$WIN" ]; then
  record FAIL $PH "TEST_BIG_RMA=$RMA 必须 >0 且不超过窗口 $WIN"
  summary; exit 1
fi
info "规模：$((BIG / 1048576)) MiB，注册窗口 $((WIN / 1024)) KiB，单次 RMA $RMA 字节"

SRCS="$TESTS_DIR/src"

# judge <说明> <文件> <模式>
judge() { if grep -q -- "$3" "$2" 2>/dev/null; then record PASS $PH "$1"; else record FAIL $PH "$1"; fi; }

# 1) 卡端：只用 libscif（不涉及 COI，也不需要 -rdynamic）。
#    优化档固定 -O0：这条链上有「-O1/-O2 的向量化产物在卡上崩」的先例（见 tests/README.md 第七节），
#    传输带宽由 PCIe 决定，与卡端代码优化档无关，稳为先。
SRV="$LOGD/bigxfer_srv"
# 注意：不要传 -I/-L —— 卡端要链的是 k1om sysroot 里的 libscif；
# 传宿主路径会把 LoongArch 的 libscif 递给交叉链接器。
# 优化档：-O2 且关掉向量化（-fno-tree-vectorize）—— 卡端有「向量化产物崩溃」的先例（见第七节），
# 而这里的校验和循环是热点，-O0 时卡端只能跑 ~130 MB/s，会成为整体瓶颈。
BIGOPT="${TEST_BIG_OPT:--O2 -fno-tree-vectorize}"
k1om_cc $BIGOPT "$SRCS/bigxfer_srv.c" -lscif -o "$SRV" >>"$LOGD/$PH.log" 2>&1
if [ -f "$SRV" ]; then
  record PASS $PH "交叉编译卡端 bigxfer_srv"
else
  record FAIL $PH "bigxfer_srv 编译失败（详见 $LOGD/$PH.log）"; summary; exit 1
fi

# 2) 宿主
CLI="$LOGD/bigxfer_cli"
gcc -O2 "$SRCS/bigxfer_cli.c" -I"$SCIF_INC" -L"$SCIF_LIB" -lscif \
    -Wl,-rpath,"$SCIF_LIB" -o "$CLI" >>"$LOGD/$PH.log" 2>&1
[ -f "$CLI" ] && record PASS $PH "编译宿主 bigxfer_cli" \
              || { record FAIL $PH "宿主编译失败（详见 $LOGD/$PH.log）"; summary; exit 1; }

# 3) 部署到卡
SRVLOG="$LOGD/bigxfer_srv.log"; CLILOG="$LOGD/bigxfer_cli.log"
rm -f "$SRVLOG" "$CLILOG"
# 上一次跑残留的卡端进程会占住端口、也会让覆盖写报 Text file busy，先清干净
card 'pkill -x bigxfer_srv 2>/dev/null; sleep 1; rm -f ~/bigxfer_srv; true' >/dev/null 2>&1
if deploy_card_file "$SRV" '~/bigxfer_srv'; then
  record PASS $PH "部署卡端程序并校验 md5"
else
  card 'pkill -x bigxfer_srv 2>/dev/null; sleep 2; rm -f ~/bigxfer_srv; true' >/dev/null 2>&1
  deploy_card_file "$SRV" '~/bigxfer_srv' \
    && record PASS $PH "部署卡端程序并校验 md5（重试成功）" \
    || { record FAIL $PH "部署失败（卡端可能仍有同名进程占着文件）"; summary; exit 1; }
fi

# 4) 收发：卡端起服务，宿主推数据。两边都给足超时（4 GiB 受 PCIe 链路宽度限制）
( timeout 900 ssh $SSHOPT "$CARD" "~/bigxfer_srv $BIG $WIN 2>&1" >"$SRVLOG" 2>&1 ) &
# 等卡端 malloc + bind + listen 就绪（最多 90 s；4 GiB 的逐页 touch 要几秒）
for _ in $(seq 1 90); do
  grep -q "BIGXFER_SRV: listening" "$SRVLOG" 2>/dev/null && break
  sleep 1
done
if ! grep -q "BIGXFER_SRV: listening" "$SRVLOG" 2>/dev/null; then
  record FAIL $PH "卡端未能在 90 s 内进入 listen（详见 $SRVLOG）"
  tail -n 5 "$SRVLOG" 2>/dev/null | sed 's/^/      /'
  summary; exit 1
fi
LD_LIBRARY_PATH="$SCIF_LIB" "$CLI" "$BIG" "$WIN" "$RMA" >"$CLILOG" 2>&1 &
CLIPID=$!
RC=0
for _ in $(seq 1 900); do
  kill -0 "$CLIPID" 2>/dev/null || break
  sleep 1
done
if kill -0 "$CLIPID" 2>/dev/null; then
  # 注意：卡在内核态（D）的进程连 SIGKILL 都收不到，只能由卸载模块清理，
  # 所以这里不硬杀，只把它记为失败并继续，避免整个阶段被拖死。
  record FAIL $PH "客户端 900 s 未结束（可能卡在驱动的窗口回收路径，详见 $CLILOG）"
  RC=124
else
  wait "$CLIPID"; RC=$?
fi
wait 2>/dev/null

# 5) 判据
judge "卡端大块 malloc 成功（$((BIG / 1048576)) MiB 并逐页 touch）" \
      "$SRVLOG" "BIGXFER_SRV: malloc $BIG ok" \
  || true

if grep -q "BIGXFER_SRV: registered" "$SRVLOG"; then
  RSPAN=$(sed -n 's/.*registered \([0-9]*\) bytes.*/\1/p' "$SRVLOG" | head -n 1)
  RUS=$(sed -n 's/.*registered [0-9]* bytes at 0x[0-9a-f]* (\([0-9.]*\) s).*/\1/p' "$SRVLOG" | head -n 1)
  record PASS $PH "卡端注册窗口 $(( ${RSPAN:-0} / 1048576 )) MiB（耗时 ${RUS:-?} s）"
else
  record FAIL $PH "卡端 scif_register 失败（详见 $SRVLOG）"
fi

MODE=$(sed -n 's/.*card mode=\([a-z]*\) .*/\1/p' "$CLILOG" | head -n 1)
SPAN=$(sed -n 's/.*span=\([0-9]*\) mode=.*/\1/p' "$CLILOG" | head -n 1)
SENT=$(sed -n 's/.*sent=\([0-9]*\) span=.*/\1/p' "$CLILOG" | head -n 1)
SEC=$(sed -n 's/.*transfer_seconds=\([0-9.]*\) .*/\1/p' "$CLILOG" | head -n 1)
BW=$(sed -n 's/.*bandwidth_MBps=\([0-9.]*\) .*/\1/p' "$CLILOG" | head -n 1)
WBW=$(sed -n 's/.*wire_bandwidth_MBps=\([0-9.]*\) .*/\1/p' "$CLILOG" | head -n 1)
HCK=$(sed -n 's/.*host_checksum=\(0x[0-9a-f]*\) .*/\1/p' "$CLILOG" | head -n 1)
CCK=$(sed -n 's/.*card_checksum=\(0x[0-9a-f]*\) .*/\1/p' "$CLILOG" | head -n 1)
CCS=$(sed -n 's/.*card_cksum_seconds=\([0-9.]*\).*/\1/p' "$CLILOG" | head -n 1)

if [ "$RC" = "0" ] && grep -q "BIGXFER RESULT=OK" "$CLILOG"; then
  record PASS $PH "宿主退出码 0 且结果 OK"
else
  if grep -q "BIGXFER RESULT=PARTIAL" "$CLILOG"; then
    record FAIL $PH "只传了 $(( ${SENT:-0} / 1048576 )) MiB / $((BIG / 1048576)) MiB：卡端最大可注册跨度仅 $(( ${SPAN:-0} / 1048576 )) MiB"
  else
    record FAIL $PH "传输未成功（退出码 $RC，详见 $CLILOG）"
  fi
  tail -n 10 "$CLILOG" | sed 's/^/      /'
fi

# 全量 vs 部分：这是「4 GiB 真的送过去了」的判据
if [ -n "${SENT:-}" ] && [ "$SENT" = "$BIG" ]; then
  record PASS $PH "传输覆盖全部 $((BIG / 1048576)) MiB（模式 $MODE，注册跨度 $(( ${SPAN:-0} / 1048576 )) MiB）"
else
  record FAIL $PH "传输未覆盖全量（sent=${SENT:-?} 期望 $BIG）"
fi

# 两端分别 checksum 并比对 —— 这是「数据没出错」的判据
if [ -n "${HCK:-}" ] && [ "$HCK" = "$CCK" ]; then
  record PASS $PH "两端校验和一致（$HCK，卡端算 $CCS s）"
else
  record FAIL $PH "两端校验和不一致（宿主 ${HCK:-无} vs 卡端 ${CCK:-无}）"
fi

# 带宽
if [ -n "${BW:-}" ] && awk "BEGIN{exit !($BW > 0)}" 2>/dev/null; then
  record PASS $PH "端到端 ${BW} MB/s（${SEC:-?} s）；链路侧 ${WBW:-?} MB/s（窗口 $((WIN / 1024)) KiB，单次 RMA $RMA 字节）"
else
  record FAIL $PH "未测出带宽（详见 $CLILOG）"
fi

judge "卡端收尾正常" "$SRVLOG" "BIGXFER_SRV: done" || true

UN=$(dmesg 2>/dev/null | grep -ci unaligned)
EX=$(dmesg 2>/dev/null | grep -cE 'Unable to handle|Oops|BUG: ')
[ "$UN" = "0" ] && record PASS $PH "无非对齐异常" || record FAIL $PH "非对齐异常 $UN 条"
[ "$EX" = "0" ] && record PASS $PH "无内核异常" || record FAIL $PH "内核异常 $EX 条"

summary
