#!/bin/bash
# 功能测试入口：T2 卡访问 → T3 SCIF 通讯 → T4 DMA/RMA → T5 COI offload → T6 较高压力 → T7 N 体重计算
#               → T8 大数据量传输（默认 4 GiB，两端 checksum + 带宽）。
#
# 前提：系统已完成编译与安装、mic 模块已加载、MPSS 栈已启动、卡已 online，
#       且 /dev/mic/scif 对**普通用户**可读写（tests/t1_install.sh 会修好）。
#
# 重要：本入口**不需要 root** —— 客户端二进制应以普通用户身份运行：
#   bash tests/run_tests.sh                 # 全部功能测试（完整规模）
#   bash tests/run_tests.sh --quick         # 较小规模，快速回归
#   bash tests/run_tests.sh --only t5       # 只跑某一阶段（t2…t8）
set -u
TESTS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$TESTS_DIR/lib/common.sh"

ONLY=""; QUICK=0
while [ $# -gt 0 ]; do
  case "$1" in
    --only)  ONLY="${2:-}"; shift;;
    --quick) QUICK=1; export TEST_QUICK=1
             export TEST_N="${TEST_N:-20000000}" TEST_N2="${TEST_N2:-200000000}" TEST_N3="${TEST_N3:-100000000}"
             export TEST_BIG="${TEST_BIG:-268435456}";;   # T8 降到 256 MiB
    -h|--help) sed -n '2,10p' "$0"; exit 0;;
    *) echo "未知参数: $1"; exit 2;;
  esac
  shift
done

printf '################ 功能测试（不含编译/安装）################\n'
printf '  内核     : %s\n' "$KVER"
printf '  卡地址   : %s\n' "$CARD"
printf '  发布树   : %s\n' "$RELEASE_DIR"
printf '  日志目录 : %s\n' "$LOGD"
printf '  规模     : %s\n' "$([ "$QUICK" = 1 ] && echo 快速 || echo 完整)"

run_phase() {   # run_phase <标签> <脚本>
  local name="$1" script="$2"
  [ -n "$ONLY" ] && [ "$ONLY" != "$name" ] && return 0
  [ -f "$script" ] || { printf '\n[SKIP] %s（缺少 %s）\n' "$name" "$script"; return 0; }
  printf '\n################ %s ################\n' "$name"
  bash "$script" || true
}

# 前置状态提示（不阻断：由各阶段自行判定）
printf '\n--- 前置状态 ---\n'
printf '  运行身份      : %s%s\n' "$(id -un)" "$(is_root && echo '  ⚠ 建议以普通用户运行（客户端不应需要 root）' || echo '')"
printf '  mic 模块      : %s\n' "$(lsmod | awk '/^mic /{print "已加载"}' | head -n1)"
printf '  卡状态        : %s\n' "$(cat /sys/class/mic/mic0/state 2>/dev/null)"
printf '  ssh 可达      : %s\n' "$(card 'echo ok' 2>/dev/null)"
printf '  卡端 daemon   : %s\n' "$(card 'pidof coi_daemon' 2>/dev/null)"
if scif_ok; then
  printf '  设备可访问    : 是 ✓（%s）\n' "$scif_dev"
else
  printf '  设备可访问    : 否 ✗（%s）\n' "$scif_dev"
  scif_hint
fi

run_phase t2 "$TESTS_DIR/t2_ssh.sh"
run_phase t3 "$TESTS_DIR/t3_scif_comm.sh"
run_phase t4 "$TESTS_DIR/t4_rma_dma.sh"
run_phase t5 "$TESTS_DIR/t5_offload.sh"
run_phase t6 "$TESTS_DIR/t6_offload_stress.sh"
run_phase t7 "$TESTS_DIR/t7_nbody.sh"
run_phase t8 "$TESTS_DIR/t8_bigxfer.sh"

printf '\n################ 功能测试结束 ################\n'
printf '  各阶段 PASS/FAIL 见上；日志在 %s/*.log\n' "$LOGD"
