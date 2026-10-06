#!/bin/bash
# 测试套件的「按站点配置」样例。
#
# 用法：
#     cp tests/config.example.sh tests/config.sh
#     然后按自己的环境填写 tests/config.sh
#
# 约定：
#   * tests/config.sh 是本地文件，通常不入库（见 .gitignore）；
#   * **环境变量优先于本文件** —— 临时改一项不必编辑文件，例如
#         CARD_HOST=192.168.1.2 bash tests/run_tests.sh --only t2
#   * 全部留空也能跑：common.sh 会尽力自动探测；探测不到的阶段会明确地「跳过」，
#     并打印需要设置哪个变量，而不是用错的值硬跑。
#
# 下面每一项后面都写了「留空时会怎样」。

# ── 卡 ────────────────────────────────────────────────────────────────
# 卡的 IP 地址。留空时自动读 MPSS 的卡配置（/etc/mpss/mic0.conf 里 Network 行的 micip=）。
CARD_HOST=""
# 登录卡用的用户名。留空 = 运行测试的当前用户（sudo 下取 SUDO_USER）。
CARD_USER=""

# ── 发布树与安装前缀 ──────────────────────────────────────────────────
# 发布树目录：其下应有 Makefile 与 00-build-tools … 09-boot-images 各子包。
# 留空时在项目根下按 release* 自动查找（要求顶层 Makefile 里含 PKGS）。
RELEASE_DIR=""
# 安装前缀：t1 用它执行 make install，并在其下核对安装结果。
PREFIX="${PREFIX:-/usr}"
# COI 的安装位置：需要能找到 include/intel-coi/ 与（lib 或 lib64）/libcoi_host.so。
# 两种写法都可以：COI_PREFIX=/usr（标准安装）或 STAGE=/path/to/stage/usr（暂存安装）。
COI_PREFIX=""
STAGE=""

# ── 卡端工具链（只有 offload 相关阶段 t5/t6/t7 需要）───────────────────
# k1om SDK 解包目录：其下应有 opt/mpss/<版本>/sysroots/{x86_64-mpsssdk-linux,k1om-mpss-linux}。
K1OM_SDK=""
# k1om 的 C／C++ 编译器。可以直接给 SDK 里的 k1om-mpss-linux-gcc/g++，
# 也可以给一层包装脚本（例如需要用模拟器运行时）。留空时自动查找：
# PATH → SDK 内路径 → 项目内常见包装脚本（k1om-tools/bin/…）。
K1OM_CC=""
K1OM_CXX=""
# 卡端依赖库目录：宿主侧 COI 会逐个校验这些库的 ELF 机器类型，
# 因此这里要放卡端程序运行所需的全部非系统库（含自建的 libgomp.so.1）。
# 留空时优先用 SDK 自带的 k1om sysroot 库目录（若存在）。
SINK_LIBS=""

# ── 内核源码（只有 t0 编译内核模块需要）───────────────────────────────
# 留空时自动用 /lib/modules/$(uname -r)/build，再退回 /usr/src/linux-headers-$(uname -r)。
KSRC=""

# ── 测试规模（可选）──────────────────────────────────────────────────
# 覆盖各阶段的默认规模；一般不用改，命令行 --quick 也能降档。
# TEST_N=""          # t5/t6 的归约规模
# TEST_N2=""         # t6 第二轮规模
# TEST_N3=""         # t6 第三轮规模
# TEST_ROUNDS=""     # t6 轮数
# TEST_NFINAL=""     # t6 收尾规模
