#!/bin/bash
# 测试套件共用库：站点配置载入、路径自动探测、日志与断言、卡端部署、环境能力判断。
# 由各 t*.sh 通过 source 载入；不要直接执行。
#
# 设计约束（重要）：**本套件不得与某一台具体机器的布局绑死。**
#   * 所有路径都在这里集中探测，顺序一律是：环境变量 → tests/config.sh → 自动探测；
#   * 探测不到时不猜、也不用错值硬跑，而是把该阶段标为 SKIP 并说明该设哪个变量；
#   * 阶段脚本里不得出现具体 IP、用户名、绝对路径。
#   站点相关的取值请写进 tests/config.sh（从 config.example.sh 复制），不要改本文件。
#
# 约定：
#   * 每条测试打印 [PASS]/[FAIL]/[SKIP]，并写入 $LOGD/<phase>.log
#   * 需要 root 的步骤会显式检查并给出提示（不静默失败）
set -u

TESTS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# ── 0. 站点配置（可选）────────────────────────────────────────────────
if [ -f "$TESTS_DIR/config.sh" ]; then
    # shellcheck source=/dev/null
    . "$TESTS_DIR/config.sh"
fi

# 读一个配置项：环境变量优先于 config.sh（config.sh 里是普通赋值，会覆盖环境变量，
# 因此这里先看环境变量的原始值，没有才用 config.sh 赋的值）
_env_or_cfg() {  # _env_or_cfg <名称>
    local n="$1" v
    eval "v=\"\${$n:-}\""
    printf '%s' "$v"
}

# ── 1. 小工具 ────────────────────────────────────────────────────────
_first_dir() {   # _first_dir <候选目录...>：返回第一个存在的
    local d
    for d in "$@"; do [ -n "$d" ] && [ -d "$d" ] && { printf '%s' "$d"; return 0; }; done
    return 1
}
_first_file() {  # _first_file <候选文件...>
    local f
    for f in "$@"; do [ -n "$f" ] && [ -f "$f" ] && { printf '%s' "$f"; return 0; }; done
    return 1
}
# 「完整发布树」的判据：顶层 Makefile 里有 PKGS，且至少有一个子包带上游原始 Makefile
# （`Makefile.mpss`）。后一条用于把「只有包装 Makefile 的打包层」与「可直接构建的完整发布树」
# 区分开 —— 前者跑 T0/T1 必然失败，不如明确跳过并提示设置 RELEASE_DIR。
_is_release_tree() {
    [ -n "${1:-}" ] || return 1
    { [ -f "$1/Makefile" ] && grep -q '^PKGS' "$1/Makefile" 2>/dev/null; } || return 1
    local p
    for p in "$1"/*/Makefile.mpss; do [ -f "$p" ] && return 0; done
    return 1
}

# ── 2. 项目与发布树 ──────────────────────────────────────────────────
# TESTS_DIR 的父目录：tests/ 放在发布树里时它就是发布树根，放在工作区里时就是工作区。
PROJECT_HOME="$(cd "$TESTS_DIR/.." && pwd)"

RELEASE_DIR="$(_env_or_cfg RELEASE_DIR)"
if [ -z "$RELEASE_DIR" ]; then
    if _is_release_tree "$TESTS_DIR/.."; then
        RELEASE_DIR="$PROJECT_HOME"                      # tests/ 就在发布树里
    else
        for d in "$PROJECT_HOME"/release*/; do           # 工作区里的 release* 目录
            _is_release_tree "$d" && { RELEASE_DIR="$(cd "$d" && pwd)"; break; }
        done
        [ -n "$RELEASE_DIR" ] || RELEASE_DIR=""          # 找不到就留空，由各阶段跳过
    fi
fi

# 开发期资产（k1om SDK、工具链包装脚本、暂存安装目录）的搜索根。
# 它们不一定与发布树同处一棵树：发布树里带的只有包，工具链通常在工作区或家目录。
# 依次尝试：发布树的上一级 → tests 的父目录 → 调用者家目录。
WORKSPACE=""
for d in "${RELEASE_DIR:+$(dirname "$RELEASE_DIR")}" "$PROJECT_HOME" "${HOME:-}"; do
    [ -n "$d" ] && [ -d "$d" ] && { WORKSPACE="$d"; break; }
done
[ -n "$WORKSPACE" ] || WORKSPACE="$PROJECT_HOME"

# ── 3. 安装前缀与 COI 位置 ───────────────────────────────────────────
PREFIX="$(_env_or_cfg PREFIX)"; PREFIX="${PREFIX:-/usr}"
STAGE="$(_env_or_cfg STAGE)"
COI_PREFIX="$(_env_or_cfg COI_PREFIX)"
COI_INC=""; COI_LIB=""
while read -r cand; do
    [ -n "$cand" ] || continue
    if [ -e "$cand/include/intel-coi" ] || [ -e "$cand/include/COIBuffer_source.h" ]; then
        for libd in "$cand/lib64" "$cand/lib"; do
            if [ -e "$libd/libcoi_host.so" ] || [ -e "$libd/libcoi_host.so.0" ]; then
                COI_INC="$cand/include"; COI_LIB="$libd"; break 2
            fi
        done
    fi
done < <(printf '%s\n' "$STAGE" "$COI_PREFIX/usr" "$COI_PREFIX" \
                     "$WORKSPACE/mpss-userland/stage/usr" "$PROJECT_HOME/mpss-userland/stage/usr" \
                     "${HOME:-}/mpss-userland/stage/usr" "$PREFIX" /usr/local /usr \
         | awk 'NF && !seen[$0]++')

# 用户态 SCIF 库（t3/t4 的宿主客户端要链它；标准安装时在 $PREFIX 下，暂存安装时在 STAGE 下）
SCIF_INC=""; SCIF_LIB=""
while read -r cand; do
    [ -n "$cand" ] || continue
    [ -e "$cand/include/scif.h" ] || continue
    for libd in "$cand/lib64" "$cand/lib"; do
        if [ -e "$libd/libscif.so" ] || [ -e "$libd/libscif.so.0" ]; then
            SCIF_INC="$cand/include"; SCIF_LIB="$libd"; break 2
        fi
    done
done < <(printf '%s\n' "$STAGE" "$COI_PREFIX/usr" "$COI_PREFIX" \
                     "$WORKSPACE/mpss-userland/stage/usr" "$PROJECT_HOME/mpss-userland/stage/usr" \
                     "${HOME:-}/mpss-userland/stage/usr" "$PREFIX" /usr/local /usr \
         | awk 'NF && !seen[$0]++')

# ── 4. 卡端工具链（offload 阶段需要）─────────────────────────────────
K1OM_SDK="$(_env_or_cfg K1OM_SDK)"
if [ -z "$K1OM_SDK" ]; then
    K1OM_SDK="$(_first_dir "$WORKSPACE"/k1om-sdk "$PROJECT_HOME"/k1om-sdk "${HOME:-}"/k1om-sdk \
                             /opt/mpss/sdk /opt/mpss || true)"
fi

# k1om sysroot（含 usr/include/intel-coi 与 usr/lib64/libcoi_device.so）
KSYS=""
if [ -n "$K1OM_SDK" ]; then
    for d in "$K1OM_SDK"/opt/mpss/*/sysroots/k1om-mpss-linux \
             "$K1OM_SDK"/sysroots/k1om-mpss-linux "$K1OM_SDK"; do
        [ -d "$d/usr/include" ] && { KSYS="$d"; break; }
    done
fi

# 直接调用 SDK 编译器时需要 -B 指路的两个目录
K1OM_TCBIN=""; K1OM_GCCLIB=""
if [ -n "$K1OM_SDK" ]; then
    K1OM_TCBIN="$(_first_dir "$K1OM_SDK"/opt/mpss/*/sysroots/x86_64-mpsssdk-linux/usr/bin/k1om-mpss-linux || true)"
    K1OM_GCCLIB="$(_first_dir "$K1OM_SDK"/opt/mpss/*/sysroots/x86_64-mpsssdk-linux/usr/libexec/k1om-mpss-linux/gcc/k1om-mpss-linux/* || true)"
fi

K1OM_CC="$(_env_or_cfg K1OM_CC)"
K1OM_CXX="$(_env_or_cfg K1OM_CXX)"
if [ -z "$K1OM_CC" ]; then
    for c in "$WORKSPACE/k1om-tools/bin/k1om-cc" "$WORKSPACE/k1om-tools/bin/k1om-cxx" \
             "$PROJECT_HOME/k1om-tools/bin/k1om-cc" "$PROJECT_HOME/k1om-tools/bin/k1om-cxx" \
             "${HOME:-}/k1om-tools/bin/k1om-cc" "${HOME:-}/k1om-tools/bin/k1om-cxx"; do
        [ -x "$c" ] && { K1OM_CC="$c"; break; }
    done
fi
[ -n "$K1OM_CC" ] || K1OM_CC="$(command -v k1om-mpss-linux-gcc 2>/dev/null || true)"
[ -n "$K1OM_CC" ] || [ -z "$K1OM_TCBIN" ] || K1OM_CC="$K1OM_TCBIN/k1om-mpss-linux-gcc"
if [ -z "$K1OM_CXX" ]; then
    for c in "$WORKSPACE/k1om-tools/bin/k1om-cxx" "$WORKSPACE/k1om-tools/bin/k1om-cc" \
             "$PROJECT_HOME/k1om-tools/bin/k1om-cxx" "$PROJECT_HOME/k1om-tools/bin/k1om-cc" \
             "${HOME:-}/k1om-tools/bin/k1om-cxx" "${HOME:-}/k1om-tools/bin/k1om-cc"; do
        [ -x "$c" ] && { K1OM_CXX="$c"; break; }
    done
fi
[ -n "$K1OM_CXX" ] || K1OM_CXX="$(command -v k1om-mpss-linux-g++ 2>/dev/null || true)"
[ -n "$K1OM_CXX" ] || [ -z "$K1OM_TCBIN" ] || K1OM_CXX="$K1OM_TCBIN/k1om-mpss-linux-g++"

# 直接用 SDK 编译器时补 -B 与 --sysroot；用包装脚本时由包装脚本自己负责
K1OM_CC_EXTRA=""; K1OM_CXX_EXTRA=""
if [ -n "$K1OM_TCBIN" ] && [ "$K1OM_CC" = "$K1OM_TCBIN/k1om-mpss-linux-gcc" ]; then
    K1OM_CC_EXTRA="-B$K1OM_TCBIN/ ${K1OM_GCCLIB:+-B$K1OM_GCCLIB/} ${KSYS:+--sysroot=$KSYS}"
    K1OM_CXX_EXTRA="$K1OM_CC_EXTRA"
fi

# 编译卡端程序的统一入口：阶段脚本一律用这两个函数，不要自己拼变量
k1om_cc()  { [ -n "$K1OM_CC" ]  || return 127; "$K1OM_CC"  $K1OM_CC_EXTRA  "$@"; }
k1om_cxx() { [ -n "$K1OM_CXX" ] || return 127; "$K1OM_CXX" $K1OM_CXX_EXTRA "$@"; }

# 卡端依赖库目录（宿主侧 COI 会逐个校验其 ELF 机器类型）
COSLIB="$(_env_or_cfg SINK_LIBS)"
[ -n "$COSLIB" ] || COSLIB="$(_env_or_cfg COSLIB)"            # 兼容旧名
[ -n "$COSLIB" ] || COSLIB="$(_first_dir "$WORKSPACE/k1om-sinklibs" "$PROJECT_HOME/k1om-sinklibs" \
                                            "${HOME:-}/k1om-sinklibs" || true)"
[ -n "$COSLIB" ] || { [ -n "$KSYS" ] && [ -d "$KSYS/usr/lib64" ] && COSLIB="$KSYS/usr/lib64"; }

# 卡端 objdump（统计指令用；宿主 binutils 通常不认识卡端的机器类型）
K1OM_OBJDUMP=""
for d in "$K1OM_TCBIN" "$K1OM_SDK"/opt/mpss/*/sysroots/*/usr/bin/k1om-mpss-linux; do
    [ -n "$d" ] && [ -x "$d/k1om-mpss-linux-objdump" ] && { K1OM_OBJDUMP="$d/k1om-mpss-linux-objdump"; break; }
done

# ── 5. 内核源码与模块安装目录 ────────────────────────────────────────
KVER="$(uname -r)"
KSRC="$(_env_or_cfg KSRC)"
[ -n "$KSRC" ] || KSRC="$(_first_dir "/lib/modules/$KVER/build" "/usr/src/linux-headers-$KVER" || true)"
KODIR=""
if command -v modinfo >/dev/null 2>&1; then
    _m="$(modinfo -n mic 2>/dev/null || true)"
    [ -n "$_m" ] && KODIR="${_m%/*}"
fi
[ -n "$KODIR" ] || KODIR="$(_first_dir "/lib/modules/$KVER/updates" "/lib/modules/$KVER/extra" "/lib/modules/$KVER" || true)"

# ── 6. 卡与 SSH ──────────────────────────────────────────────────────
CARD_HOST="$(_env_or_cfg CARD_HOST)"
if [ -z "$CARD_HOST" ]; then
    # MPSS 把卡的地址写在 /etc/mpss/mic0.conf 的 Network 行（micip=…）
    for f in /etc/mpss/mic0.conf /etc/mpss/default.conf; do
        [ -f "$f" ] || continue
        CARD_HOST="$(sed -n 's/.*[[:space:]]micip=\([0-9][0-9.]*\).*/\1/p' "$f" | head -n 1)"
        [ -n "$CARD_HOST" ] && break
    done
fi
# 以 sudo 运行时 $HOME 是 /root，密钥其实在调用者家目录，从 SUDO_USER 推导
REAL_USER="${SUDO_USER:-$(id -un)}"
REAL_HOME="$(getent passwd "$REAL_USER" 2>/dev/null | cut -d: -f6)"
[ -n "${REAL_HOME:-}" ] || REAL_HOME="$HOME"
CARD_USER="$(_env_or_cfg CARD_USER)"; CARD_USER="${CARD_USER:-$REAL_USER}"
CARD="${CARD_USER}@${CARD_HOST}"
SSHKEY="${SSHKEY:-$REAL_HOME/.ssh/id_ed25519}"
[ -f "$SSHKEY" ] || SSHKEY="$REAL_HOME/.ssh/id_rsa"
SSHOPT="-o BatchMode=yes -o StrictHostKeyChecking=accept-new -o UserKnownHostsFile=$REAL_HOME/.ssh/known_hosts -o ConnectTimeout=8"
[ -f "$SSHKEY" ] && SSHOPT="$SSHOPT -i $SSHKEY"

# ── 7. 日志目录 ──────────────────────────────────────────────────────
# 每次运行一个独立子目录，避免历史文件（尤其曾以 root 运行留下的 root 属主产物）
# 让普通用户覆盖不了而出现「假失败」。
LOGD="${TEST_LOGD:-$TESTS_DIR/logs}"
mkdir -p "$LOGD" 2>/dev/null || true
if [ ! -w "$LOGD" ] || [ ! -x "$LOGD" ]; then
    LOGD="$REAL_HOME/.mpss-tests-logs"; mkdir -p "$LOGD" 2>/dev/null || true
fi
LOGD="$LOGD/run-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$LOGD" 2>/dev/null || true
if [ ! -w "$LOGD" ]; then
    LOGD="$REAL_HOME/.mpss-tests-logs/run-$(date +%Y%m%d-%H%M%S)"
    mkdir -p "$LOGD" 2>/dev/null || true
fi

# ── 8. 输出与记账 ────────────────────────────────────────────────────
say()  { printf '%s\n' "$*"; }
info() { printf '  %s\n' "$*"; }
head1(){ printf '\n===== %s =====\n' "$*"; }

is_root() { [ "$(id -u)" = "0" ]; }

PASS_N=0; FAIL_N=0; SKIP_N=0
declare -a RESULTS=()

record() {   # record <PASS|FAIL|SKIP> <阶段> <说明>
    local st="$1" ph="$2" msg="$3"
    case "$st" in
        PASS) PASS_N=$((PASS_N+1)); printf '  [PASS] %-6s %s\n' "$ph" "$msg";;
        FAIL) FAIL_N=$((FAIL_N+1)); printf '  [FAIL] %-6s %s\n' "$ph" "$msg";;
        SKIP) SKIP_N=$((SKIP_N+1)); printf '  [SKIP] %-6s %s\n' "$ph" "$msg";;
    esac
    RESULTS+=("$st|$ph|$msg")
}

assert_ok() {  # assert_ok <阶段> <说明> <命令...>
    local ph="$1" msg="$2"; shift 2
    if "$@" >>"$LOGD/$ph.log" 2>&1; then record PASS "$ph" "$msg"; else record FAIL "$ph" "$msg"; fi
}

summary() {
    local total=$((PASS_N+FAIL_N+SKIP_N))
    printf '\n================ 测试汇总 ================\n'
    printf '  通过 %d    失败 %d    跳过 %d    共 %d\n' "$PASS_N" "$FAIL_N" "$SKIP_N" "$total"
    if [ "$FAIL_N" -gt 0 ]; then
        printf '  --- 失败项 ---\n'
        local r
        for r in "${RESULTS[@]}"; do case "$r" in FAIL*) printf '    %s\n' "${r#FAIL|}";; esac; done
    fi
    if [ "$SKIP_N" -gt 0 ]; then
        printf '  --- 跳过项（多为本机未提供该项前置；按上面的提示设置即可）---\n'
        local r
        for r in "${RESULTS[@]}"; do case "$r" in SKIP*) printf '    %s\n' "${r#SKIP|}";; esac; done
    fi
    printf '  日志目录: %s\n' "$LOGD"
    [ "$FAIL_N" -eq 0 ] && return 0 || return 1
}

# ── 9. 环境能力判断（各阶段据此决定「跑」还是「跳过」）───────────────
has_release_tree() { _is_release_tree "${RELEASE_DIR:-}"; }
has_coi()          { [ -n "$COI_INC" ] && [ -n "$COI_LIB" ]; }
has_scif()         { [ -n "$SCIF_LIB" ]; }
has_k1om_cc()      { [ -n "$K1OM_CC" ]; }
has_k1om_cxx()     { [ -n "$K1OM_CXX" ]; }
has_sysroot()      { [ -n "$KSYS" ] && [ -d "$KSYS/usr/include" ]; }
has_sink_libs()    { [ -n "$COSLIB" ] && [ -d "$COSLIB" ]; }
has_gomp()         { has_sink_libs && ls "$COSLIB"/libgomp.so* >/dev/null 2>&1; }
has_kernel_src()   { [ -n "$KSRC" ] && [ -d "$KSRC" ]; }
has_objdump()      { [ -n "$K1OM_OBJDUMP" ] && [ -x "$K1OM_OBJDUMP" ]; }
card_configured()  { [ -n "$CARD_HOST" ]; }

need_release_tree() { has_release_tree && return 0; info "缺发布树：请设 RELEASE_DIR，或在 tests/config.sh 里指定（当前：'${RELEASE_DIR:-未设}'）"; return 1; }
need_coi()          { has_coi && return 0; info "缺 COI：请设 STAGE（含 include/ 与 lib64/ 的目录）或 COI_PREFIX（例如 /usr）"; return 1; }
need_k1om_cxx()     { has_k1om_cxx && has_sysroot && return 0; info "缺 k1om 工具链：请设 K1OM_SDK（含 opt/mpss/*/sysroots）或 K1OM_CXX"; return 1; }
need_k1om_cc()      { has_k1om_cc && has_sysroot && return 0; info "缺 k1om 工具链：请设 K1OM_SDK（含 opt/mpss/*/sysroots）或 K1OM_CC"; return 1; }
need_scif()         { has_scif && return 0; info "缺用户态 SCIF 库：请设 STAGE（含 include/scif.h 与 libscif.so）或 COI_PREFIX（例如 /usr）"; return 1; }
need_sink_libs()    { has_sink_libs && return 0; info "缺卡端依赖库目录：请设 SINK_LIBS（需含 libcoi_device 与 libgomp 等卡端库）"; return 1; }
need_gomp()         { has_gomp && return 0; info "依赖库目录里没有 libgomp.so（卡端 OpenMP 运行时）：请把自建的 k1om libgomp 放进 SINK_LIBS 指向的目录"; return 1; }
need_kernel_src()   { has_kernel_src && return 0; info "缺内核源码或头：请设 KSRC（默认试 /lib/modules/$(uname -r)/build）"; return 1; }
need_card_host()    { card_configured && return 0; info "不知道卡的地址：请设 CARD_HOST，或让 MPSS 在 /etc/mpss/mic0.conf 里配好 micip="; return 1; }

need_root() {
    if ! is_root; then
        info "需要 root：$*（安装类操作请用 sudo 跑）"
        return 1
    fi
    return 0
}

# ── 10. 卡端访问 ─────────────────────────────────────────────────────
# 普通用户能否使用 SCIF 设备（这才是「使用场景」的门槛，而非 root）。
# 注意：SCIF 是字符设备且不可 seek，不能用 python 的 "r+b" 打开（会报 not seekable），
# 这里用 shell 重定向打开，语义与真实客户端一致。
scif_dev="${SCIF_DEV:-/dev/mic/scif}"
scif_ok() {
    [ -e "$scif_dev" ] || return 1
    # exec 是 POSIX 特殊内建：重定向失败会让**整个 shell 退出**，
    # 所以试开动作必须放进子 shell —— 否则权限不对时脚本当场死掉，
    # 而不是走到下面那句可照做的提示。
    ( exec 3<>"$scif_dev" ) 2>/dev/null
}

# 打不开设备时给出可照做的修法（设备权限由 55-mic-perms.rules 负责）
scif_hint() {
    local mode dir
    mode="$(stat -c %a "$scif_dev" 2>/dev/null || echo '?')"
    dir="$(dirname "$scif_dev")"
    info "$scif_dev 普通用户打不开（当前模式 $mode）。修法二选一："
    info "  sudo bash tests/t1_install.sh            # 装 55-mic-perms.rules 并立刻放开（推荐）"
    info "  sudo chmod 666 $scif_dev $dir/ctrl; sudo chmod o+x $dir   # 临时，重载模块后失效"
}

card() {   # card <命令...>：在卡上执行（静默 ssh 噪音）
    card_configured || return 1
    timeout 30 ssh $SSHOPT "$CARD" "$@" 2>/dev/null
}

card_up() { card_configured && [ -n "$(card 'echo ok')" ]; }

# 部署一个文件到卡端并校验 md5
deploy_card_file() {  # deploy_card_file <本地文件> <卡端路径>
    local src="$1" dst="$2"
    [ -f "$src" ] || { info "缺文件 $src"; return 1; }
    local m1 m2
    m1=$(md5sum "$src" | cut -c1-32)
    # 先删旧文件：若卡上同名程序正在运行，直接覆盖会报 "Text file busy"（ETXTBSY）
    card "rm -f $dst" >/dev/null 2>&1
    card "cat > $dst && chmod +x $dst" < "$src" >/dev/null 2>&1 || return 1
    m2=$(card "md5sum $dst 2>/dev/null | cut -c1-32" | tr -d '\r')
    [ "$m1" = "$m2" ]
}

wait_online() {  # wait_online <最多秒>
    local max="${1:-300}" i
    for i in $(seq 1 $((max/5))); do
        [ "$(cat /sys/class/mic/mic0/state 2>/dev/null)" = "online" ] && return 0
        sleep 5
    done
    return 1
}

wait_daemon() {  # wait_daemon <最多秒>
    local max="${1:-120}" i p
    for i in $(seq 1 $((max/5))); do
        p=$(card 'pidof coi_daemon' | tr -d '\r')
        [ -n "$p" ] && { echo "$p"; return 0; }
        sleep 5
    done
    return 1
}

card_state() { cat /sys/class/mic/mic0/state 2>/dev/null; }
