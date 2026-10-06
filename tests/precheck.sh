#!/bin/bash
# 测试套件自检：交叉编译卡端测例、编译宿主测例，检查产物与导出符号。
#   用途：功能测试（run_tests.sh）跑不起来时先跑这个，把「编译问题」与「运行问题」分开。
#   用法：bash tests/precheck.sh          （普通用户即可，不需要 root，不接触卡）
set -u
source "$(dirname "${BASH_SOURCE[0]}")/lib/common.sh"

OUT="$LOGD/precheck"; mkdir -p "$OUT"
has_scif  || { info "缺用户态 SCIF 库：设 STAGE 或 COI_PREFIX 后重试"; exit 0; }
has_coi   || { info "缺 COI 宿主库：设 STAGE 或 COI_PREFIX 后重试"; exit 0; }
need_k1om_cxx || exit 0
SRCS="$TESTS_DIR/src"

head1 "0 工具链与依赖"
info "k1om C 编译器   : ${K1OM_CC:-未设} $(has_k1om_cc && echo ✓ || echo ✗)"
info "k1om C++ 编译器 : ${K1OM_CXX:-未设} $(has_k1om_cxx && echo ✓ || echo ✗)"
info "k1om sysroot    : $KSYS $([ -d "$KSYS" ] && echo ✓ || echo ✗)"
info "k1om 依赖库目录 : $COSLIB $([ -d "$COSLIB" ] && echo "✓（$(ls "$COSLIB" 2>/dev/null | wc -l) 个文件）" || echo ✗)"
info "COI 头/库       : ${COI_INC:-未设} / ${COI_LIB:-未设} $(has_coi && echo ✓ || echo ✗)"
info "发布树          : $RELEASE_DIR"
info "卡（不连接，仅显示）: $CARD"

head1 "1 卡端测例（交叉编译）"
build_card() {   # build_card <源文件> <附加参数...>
  local src="$1"; shift
  local name; name="$(basename "$src" .c)"; name="$(basename "$name" .cpp)"
  k1om_cc -O2 -o "$OUT/$name" "$src" "$@" 2>"$OUT/$name.err" \
    && echo "  $name ✓ $(stat -c %s "$OUT/$name") 字节" \
    || { echo "  $name ✗"; head -n 8 "$OUT/$name.err" | sed 's/^/      /'; }
}
build_card "$SRCS/sc_srv.c" -lscif
build_card "$SRCS/sw_srv.c" -lscif
build_card "$SRCS/fw_srv.c" -lscif
build_card "$SRCS/bigxfer_srv.c" -lscif

head1 "2 卡端 COI sink（交叉编译，-rdynamic 必须）"
build_sink() {   # build_sink <源文件> <导出符号名>
  local src="$1" sym="$2" name; name="$(basename "$src" .cpp)"
  if k1om_cxx -O0 -fopenmp -rdynamic -I"$KSYS/usr/include" "$src" \
      -L"$KSYS/usr/lib64" -lcoi_device -L"$COSLIB" -lgomp -lpthread -ldl -lrt \
      -Wl,-rpath,/tmp -o "$OUT/$name" 2>"$OUT/$name.err"; then
    echo "  $name ✓ $(stat -c %s "$OUT/$name") 字节；$sym 在 .dynsym：$(readelf --dyn-syms "$OUT/$name" 2>/dev/null | grep -ci "$sym") 条（应 ≥1）"
  else
    echo "  $name ✗"; head -n 10 "$OUT/$name.err" | sed 's/^/      /'
  fi
}
build_sink "$SRCS/offload_sink.cpp" CardReduce
build_sink "$SRCS/nbody_sink.cpp"   NBodyRun

head1 "3 宿主测例（本机编译）"
for f in sc_cli sw_cli fw_cli bigxfer_cli; do
  gcc -O2 -o "$OUT/$f" "$SRCS/$f.c" -I"$SCIF_INC" -L"$SCIF_LIB" -lscif \
      -Wl,-rpath,"$SCIF_LIB" 2>"$OUT/$f.err" \
    && echo "  $f ✓ $(stat -c %s "$OUT/$f") 字节" \
    || { echo "  $f ✗"; head -n 8 "$OUT/$f.err" | sed 's/^/      /'; }
done
for f in offload_host nbody_host; do
  extra=""; [ "$f" = "nbody_host" ] && extra="-fopenmp"
  g++ -O2 $extra -I"$SCIF_INC" "$SRCS/$f.cpp" -o "$OUT/$f" \
      -L"$SCIF_LIB" -lcoi_host -Wl,-rpath,"$SCIF_LIB" 2>"$OUT/$f.err" \
    && echo "  $f ✓ $(stat -c %s "$OUT/$f") 字节" \
    || { echo "  $f ✗"; head -n 8 "$OUT/$f.err" | sed 's/^/      /'; }
done

echo
info "产物目录: $OUT"
