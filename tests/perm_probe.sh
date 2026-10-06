#!/bin/bash
# 验证「非 root 使用」这一使用场景：设备权限是否足够，以及普通用户能否真的跑完一次 offload。
#   用法：sudo bash tests/perm_probe.sh
#   步骤：1) 记录现状 → 2) 按 Intel 原意放开设备权限 → 3) 以普通用户验证能否打开设备
#         4) 以普通用户编译并跑一次真正的 COI offload（决定性判据）
#   说明：本脚本只改 /dev/mic/* 的运行期权限（等价于 Intel 原 udev 规则的 MODE=0666），
#         要做成开机自动生效请用 tests/t1_install.sh（它会装现代 udev 规则）。
set -u
source "$(dirname "${BASH_SOURCE[0]}")/lib/common.sh"

is_root || { echo "需要 root：sudo bash tests/perm_probe.sh"; exit 1; }
need_card_host || exit 0
need_k1om_cxx  || exit 0
OUT="$LOGD/perm-probe"; mkdir -p "$OUT"
rc=0

head1 "1 现状（以 udev 默认规则安装时的样子）"
ls -l /dev/mic/ 2>/dev/null | sed 's/^/  /'
if su -s /bin/bash -c 'exec 3<>/dev/mic/scif' "$REAL_USER" >/dev/null 2>&1; then
  info "普通用户（$REAL_USER）当前已可打开 /dev/mic/scif"
else
  info "普通用户（$REAL_USER）当前无法打开 /dev/mic/scif（预期如此：Intel 原规则的 NAME= 旧写法在 systemd-udev 下被忽略，退回 0600 root:root）"
fi

head1 "2 按 Intel 原意放开权限（等价的现代写法）"
chmod 666 /dev/mic/scif /dev/mic/ctrl 2>/dev/null && info "chmod 666 /dev/mic/{scif,ctrl} ✓"
chmod o+x /dev/mic 2>/dev/null && info "chmod o+x /dev/mic ✓"
ls -l /dev/mic/ | sed 's/^/  /'

head1 "3 以普通用户（$REAL_USER）验证设备可打开"
# 注意：SCIF 是字符设备且不可 seek，不能用 python 的 "r+b" 打开（会报 not seekable），
#      这里用 shell 重定向打开，语义与真实客户端一致（与 lib/common.sh 的 scif_ok 同一做法）。
if su -s /bin/bash -c 'exec 3<>/dev/mic/scif && exec 3<&-' "$REAL_USER" >/dev/null 2>&1; then
  info "非 root 打开 /dev/mic/scif 成功 ✓"
else
  info "非 root 打开 /dev/mic/scif 失败 ✗"
  rc=1
fi

head1 "4 以普通用户编译并跑一次真正的 COI offload（决定性验证）"
SINK="$OUT/offload_sink"; HOST="$OUT/offload_host"
if ! has_k1om_cxx || ! has_sink_libs; then
  info "跳过：缺 k1om 交叉编译器（${K1OM_CXX:-未设}）或依赖库目录（${COSLIB:-未设}）"
else
  # sink 与宿主都在 root 下编好（编译不属于「使用场景」），运行才以普通用户身份进行
  k1om_cxx -O2 -fopenmp -rdynamic -I"$KSYS/usr/include" "$TESTS_DIR/src/offload_sink.cpp" \
      -L"$KSYS/usr/lib64" -lcoi_device -L"$COSLIB" -lgomp -lpthread -ldl -lrt \
      -Wl,-rpath,/tmp -o "$SINK" >"$OUT/build.log" 2>&1
  g++ -O2 -I"$COI_INC" "$TESTS_DIR/src/offload_host.cpp" -o "$HOST" \
      -L"$COI_LIB" -lcoi_host -Wl,-rpath,"$COI_LIB" >>"$OUT/build.log" 2>&1
  if [ -f "$SINK" ] && [ -f "$HOST" ]; then
    chmod 755 "$SINK" "$HOST"
    set +e
    su -s /bin/bash -c "LD_LIBRARY_PATH='$COI_LIB' timeout 300 '$HOST' '$SINK' '${TEST_N:-200000000}' '$COSLIB'" \
        "$REAL_USER" >"$OUT/run.log" 2>&1
    RC=$?
    set -e
    grep -vE 'Entering function|Exiting function' "$OUT/run.log" | sed 's/^/    /'
    info "退出码: $RC"
    if [ "$RC" = "0" ] && grep -q '端到端完成' "$OUT/run.log"; then
      info "===> 非 root 场景验证通过 ✓✓（缺口仅在于设备权限）"
    else
      info "===> 非 root 场景未通过 ✗ —— 可能还有其他 root 依赖（见上方输出与 $OUT/run.log）"
      rc=1
    fi
  else
    info "跳过：offload 产物编译失败（详见 $OUT/build.log）"
    rc=1
  fi
fi

head1 "结论"
[ "$rc" = 0 ] && info "普通用户即可完成全部功能（符合设计目标）" \
              || info "存在非 root 无法完成的部分，见上"
info "日志目录: $OUT"
exit "$rc"
