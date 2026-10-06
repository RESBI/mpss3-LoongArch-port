#!/bin/bash
# 便利入口：编译 → 安装 → 功能测试（三者也可分别单独执行）。
#
#   sudo bash tests/t0_build.sh     # 只编译
#   sudo bash tests/t1_install.sh   # 只安装并启动到卡 online
#   sudo bash tests/run_tests.sh    # 只跑功能测试
#
#   sudo bash tests/run_all.sh                 # 三者顺序执行（默认）
#   sudo bash tests/run_all.sh --no-build      # 跳过编译
#   sudo bash tests/run_all.sh --no-install    # 跳过安装
#   sudo bash tests/run_all.sh --tests-only    # 等价于 --no-build --no-install
#   sudo bash tests/run_all.sh --quick         # 功能测试用较小规模
set -u
TESTS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$TESTS_DIR/lib/common.sh"

DO_BUILD=1; DO_INSTALL=1; QUICK=""
while [ $# -gt 0 ]; do
  case "$1" in
    --no-build)   DO_BUILD=0;;
    --no-install) DO_INSTALL=0;;
    --tests-only) DO_BUILD=0; DO_INSTALL=0;;
    --quick)      QUICK="--quick";;
    -h|--help)    sed -n '2,12p' "$0"; exit 0;;
    *) echo "未知参数: $1"; exit 2;;
  esac
  shift
done

printf '################ 全流程：编译 → 安装 → 功能测试 ################\n'
printf '  编译 : %s\n' "$([ "$DO_BUILD" = 1 ] && echo 执行 || echo 跳过)"
printf '  安装 : %s\n' "$([ "$DO_INSTALL" = 1 ] && echo 执行 || echo 跳过)"
printf '  测试 : 执行 %s\n' "$QUICK"

rc=0
if [ "$DO_BUILD" = 1 ]; then
  printf '\n################ build ################\n'
  bash "$TESTS_DIR/t0_build.sh" || rc=1
fi
if [ "$DO_INSTALL" = 1 ]; then
  printf '\n################ install ################\n'
  bash "$TESTS_DIR/t1_install.sh" || rc=1
fi

printf '\n################ tests ################\n'
# shellcheck disable=SC2086
bash "$TESTS_DIR/run_tests.sh" $QUICK || rc=1

printf '\n################ 全局结论 ################\n'
[ "$rc" = 0 ] && printf '  各阶段汇总均为通过 ✓\n' || printf '  存在失败阶段，请查看上方 PASS/FAIL 与 %s/T*.log\n' "$LOGD"
exit "$rc"
