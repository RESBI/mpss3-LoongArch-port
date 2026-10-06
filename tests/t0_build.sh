#!/bin/bash
# 编译（阶段 T0）：按依赖顺序构建 release 里的全部子项目（对应顶层 Makefile 的 all）。
#   用法：sudo bash tests/t0_build.sh
# 说明：写发布树与生成物需要 root，故由使用者带 sudo 执行。
# 本脚本只编译，不安装、不测试；安装见 tests/t1_install.sh，功能测试见 tests/run_tests.sh。
set -u
source "$(dirname "${BASH_SOURCE[0]}")/lib/common.sh"

head1 "T0 编译（按依赖顺序构建全部子包）"
info "发布树: $RELEASE_DIR"
info "内核头: $KSRC"

if ! need_root "编译（写发布树与生成物）"; then
  record SKIP T0 "未以 root 运行，跳过编译"
  summary; exit $?
fi

need_kernel_src || { record SKIP T0 "缺内核源码或头（设 KSRC）"; summary; exit 0; }
need_release_tree || { record SKIP T0 "缺发布树（设 RELEASE_DIR）"; summary; exit 0; }

: >"$LOGD/T0.log"
cd "$RELEASE_DIR" || { record FAIL T0 "无法进入发布树"; summary; exit 1; }

# 逐个包构建，便于定位失败的具体子项目
FAILED=""
for p in 00-build-tools 02-libscif 03-mpss-daemon 04-mpss-micmgmt 05-miccheck \
         06-mpss-coi 07-mpss-myo 08-mic-module 09-boot-images; do
  [ -d "$p" ] || { info "跳过不存在的 $p"; continue; }
  printf '  --- 构建 %s ...\n' "$p"
  if make -C "$p" build PREFIX="$PREFIX" >>"$LOGD/T0.log" 2>&1; then
    record PASS T0 "编译 $p"
  else
    record FAIL T0 "编译 $p（详见 $LOGD/T0.log）"
    FAILED="$FAILED $p"
  fi
done

# 关键产物存在性检查：内核模块与用户态库/程序
head1 "T0 产物检查"
check_artifact() {  # check_artifact <说明> <路径...>
  local msg="$1"; shift
  local f
  for f in "$@"; do [ -e "$f" ] && { record PASS T0 "$msg ($(basename "$f"))"; return 0; }; done
  record FAIL T0 "$msg（未找到：$*）"
}

check_artifact "宿主内核模块 mic.ko" "$RELEASE_DIR/08-mic-module/mic.ko"
check_artifact "libscif 库" "$RELEASE_DIR/02-libscif/libscif.so" "${SCIF_LIB:-/nonexistent}/libscif.so"
check_artifact "mpssd 守护进程" "$RELEASE_DIR/03-mpss-daemon/mpssd/mpssd"
check_artifact "COI 宿主库" "$RELEASE_DIR/06-mpss-coi/build/libcoi_host.so" "$RELEASE_DIR/06-mpss-coi/libcoi_host.so"
check_artifact "卡端启动镜像" "$RELEASE_DIR/09-boot-images/initramfs-knightscorner.cpio.gz"

head1 "T0 内核模块签名信息"
if [ -f "$RELEASE_DIR/08-mic-module/mic.ko" ]; then
  modinfo "$RELEASE_DIR/08-mic-module/mic.ko" 2>/dev/null | grep -E '^(filename|version|vermagic|depends)' | sed 's/^/  /'
fi

summary
