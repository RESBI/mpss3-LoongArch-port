# MPSS 3.8.6 LoongArch 移植版 · 验收测试套件

　　这套脚本把「这次移植到底能不能用」变成一组可重复执行、逐条给出 PASS/FAIL 的检查。它同时是回归护栏：`t4`／`t5`／`t7` 里的每一条判据，都对应移植过程中真实踩过的一个坑。

　　**设计约束：本套件不与任何一台具体机器的布局绑死。** 全部路径在 `lib/common.sh` 里集中解析，顺序是「环境变量 → `tests/config.sh` → 自动探测」；探测不到时不猜、也不用错值硬跑，而是把该阶段标为 `SKIP` 并打印该设哪个变量。阶段脚本里不出现具体 IP、用户名或绝对路径。

## 一、怎么用（三步）

```bash
cd tests
cp config.example.sh config.sh     # ① 复制站点配置样例
$EDITOR config.sh                  # ② 只填自动探测不出来的项（通常一两行）
bash run_tests.sh                  # ③ 跑功能测试（普通用户即可，不需要 root）
```

　　环境变量优先于 `config.sh`，临时改一项不必编辑文件：

```bash
CARD_HOST=192.168.1.2 bash run_tests.sh --only t2
```

## 二、前提与自动探测

| 用途 | 需要什么 | 探测不到时 |
|---|---|---|
| `t0` / `t1`（编译与安装） | root、内核源码或头、完整的发布树可写 | 标 `SKIP`，提示设 `KSRC`／`RELEASE_DIR` |
| `t2`–`t4`（卡访问与数据面） | **普通用户**即可；`/dev/mic/scif` 对使用者可读写；`mic` 模块已加载、MPSS 栈已启动、卡已 `online` | 标 `SKIP`，提示设 `CARD_HOST` 或让 MPSS 配好 `mic0.conf` |
| `t5`–`t7`（offload） | 另外需要 k1om 编译器、k1om sysroot、卡端依赖库目录（含自建的 k1om `libgomp`） | 标 `SKIP`，逐项提示缺的是哪一个 |

　　自动探测的覆盖面（自上而下依次尝试）：

| 项 | 变量 | 探测顺序 |
|---|---|---|
| 卡地址 | `CARD_HOST` | MPSS 的 `/etc/mpss/mic0.conf`（`micip=`） → 留空即跳过 |
| 卡登录用户 | `CARD_USER` | 默认取运行测试的用户（`sudo` 下取 `SUDO_USER`） |
| 发布树 | `RELEASE_DIR` | `tests/` 所在目录（若它是完整发布树）→ 项目根下的 `release*`。判据是顶层 `Makefile` 含 `PKGS` 且子包带 `Makefile.mpss` |
| 安装前缀 | `PREFIX` | 默认 `/usr` |
| COI 头与库 | `STAGE` 或 `COI_PREFIX` | `STAGE` → `COI_PREFIX/usr` → `COI_PREFIX` → `/usr/local` → `/usr`（要求目录里有 `include/intel-coi` 与 `libcoi_host.so`） |
| 用户态 SCIF 库 | 同上 | 同上（要求有 `include/scif.h` 与 `libscif.so`） |
| k1om SDK 与 sysroot | `K1OM_SDK` | 项目根下的 `k1om-sdk` → `/opt/mpss/sdk` → `/opt/mpss` |
| k1om 编译器 | `K1OM_CC`／`K1OM_CXX` | 项目内的包装脚本 → `PATH` → SDK 内路径（直接调 SDK 编译器时自动补 `-B` 与 `--sysroot`） |
| 卡端依赖库目录 | `SINK_LIBS` | SDK 的 k1om sysroot `usr/lib64`（若其中没有 `libgomp.so`，offload 阶段会提示你指定自己的目录） |
| k1om objdump | 自动 | SDK 内 `k1om-mpss-linux-objdump` |
| 内核源码 | `KSRC` | `/lib/modules/$(uname -r)/build` → `/usr/src/linux-headers-$(uname -r)` |
| 模块安装目录 | 自动 | `modinfo -n mic` → `/lib/modules/$(uname -r)/updates`（或 `extra`） |

## 三、阶段一览

| 阶段 | 脚本 | 测什么 | 需要 root |
|---|---|---|---|
| T0 | `t0_build.sh` | 按依赖顺序编译发布树的 9 个子包，并核对 `mic.ko`／`libscif.so`／`mpssd`／`libcoi_host.so`／卡端镜像是否产出 | 是 |
| T1 | `t1_install.sh` | `make install` → `depmod` → `modprobe mic` → 装现代 udev 规则放开设备权限 → 启动 mpss → 等卡 `online` → 卡端 `coi_daemon` | 是 |
| T2 | `t2_ssh.sh` | SSH 可达、卡端环境（内核／运行时长／内存／ramfs）、交叉编译并部署一个小程序（md5 校验）后在卡上执行 | 否 |
| T3 | `t3_scif_comm.sh` | SCIF 建链、小消息（64 B）与大消息（26496 B，即 COI 创建命令的尺寸）往返、卡端主动消息、双向 fence | 否 |
| T4 | `t4_rma_dma.sh` | RMA／`writeto` 七档长度扫描（含偏移对照与非整页长度）＋ 固定 26496 字节全量逐字节校验 | 否 |
| T5 | `t5_offload.sh` | COI 端到端：卡端 `CardReduce` 必须在 `.dynsym` → 枚举引擎 → 卡上建进程 → 建管道 → 取函数句柄 → 卡上 OpenMP 归约 → 与解析值比对 | 否 |
| T6 | `t6_offload_stress.sh` | 多轮、大规模、反复创建／销毁进程；每轮检查卡端 daemon 存活与内核无异常 | 否 |
| T7 | `t7_nbody.sh` | N 体引力 O(N²)（三档 1024／8192／16384）：卡端自生成初始条件、返回区回传、校验和与宿主参考实现逐位比对、能量守恒、GFLOPS；并统计卡端二进制里的向量指令条数（参考信息，不是判据） | 否 |
| T8 | `t8_bigxfer.sh` | 大数据量传输：默认 **4 GiB** 从宿主送到卡上 —— 卡端 4 GiB `malloc` 并逐页 touch、注册 1 MiB 窗口、分块 RMA；**逐窗口**校验 ＋ 两端整缓冲 checksum 比对；测算带宽（链路侧与端到端各一个）。真机实测：64 MiB／256 MiB／4 GiB 全通过，4 GiB 端到端 **119.3 MB/s**（`scif_writeto` 内 3478 MB/s，链路为 Gen2 ×8），两端 checksum 逐位一致。规模可用 `TEST_BIG`／`TEST_BIG_WINDOW`／`TEST_BIG_RMA` 覆盖，`--quick` 降到 256 MiB | 否 |

　　另有三个辅助脚本：

| 脚本 | 用途 | 需要 root |
|---|---|---|
| `precheck.sh` | 只编译不运行、不接触卡：把「编译问题」与「运行问题」分开（跑功能测试失败时先跑它） | 否 |
| `perm_probe.sh` | 验证「非 root 使用」这一使用场景：放开设备权限后，以普通用户**真的跑完一次 offload** | 是 |
| `run_all.sh` / `run_tests.sh` | 便利入口（见下） | 视阶段 |

## 四、怎么跑

```bash
# 全流程：编译 → 安装 → 功能测试（默认完整规模）
sudo bash run_all.sh

# 只跑功能测试（不需要 root）
bash run_tests.sh

# 单独某个阶段
bash run_tests.sh --only t7
sudo bash t0_build.sh
sudo bash t1_install.sh

# 小规模快速回归（T5/T6 的 N 自动降一档）
bash run_tests.sh --quick
```

　　`run_all.sh` 的开关：`--no-build`、`--no-install`、`--tests-only`、`--quick`。
　　规模可用环境变量覆盖：`TEST_N`（T5/T6 归约规模）、`TEST_N2`／`TEST_N3`（T6 后两轮）、`TEST_ROUNDS`（T6 轮数）、`TEST_NFINAL`（T6 收尾规模）。

　　日志按**每次运行**单独成目录，避免上一次 root 产物的属主问题造成「假失败」：

```text
tests/logs/run-<YYYYmmdd-HHMMSS>/
```

## 五、结果怎么读

| 标记 | 含义 | 该怎么办 |
|---|---|---|
| `[PASS]` | 该判据成立 | — |
| `[FAIL]` | 该判据不成立，属**被测对象或环境**的问题 | 看阶段日志与报告对应章节 |
| `[SKIP]` | 本机没提供这项前置（缺配置、缺工具链、卡不可达等） | 按打印出的提示设置变量后重跑；跳过不等于通过 |

　　汇总里会把跳过项单独列出。发布前的验收应当做到「跳过 0」；日常回归允许有跳过，但要清楚每一项为什么跳过。

## 六、目录结构

```text
tests/
├── config.example.sh     站点配置样例（复制为 config.sh 后填写；config.sh 不入库）
├── run_all.sh            编译 → 安装 → 测试 一把梭
├── run_tests.sh          功能测试入口（T2…T7）
├── t0_build.sh  t1_install.sh
├── t2_ssh.sh    t3_scif_comm.sh   t4_rma_dma.sh
├── t5_offload.sh  t6_offload_stress.sh  t7_nbody.sh
├── precheck.sh  perm_probe.sh
├── lib/common.sh         共用库：配置载入、路径探测、PASS/FAIL/SKIP 记账、卡端部署、能力判断
├── src/                  测例源码（卡端 .c/.cpp 与宿主侧一一配对）
└── CHANGELOG_CN.md       本子项目的变更记录（英文版 CHANGELOG.md）
```

## 七、几条实测约束（测例里已经替你绕开了，自己写程序时要当心）

1. **卡端程序必须 `-rdynamic`**，否则导出符号不在 `.dynsym`，卡端 `dlsym` 失败 → 宿主收到 `COI_DOES_NOT_EXIST(5)`。
2. **卡端程序的优化档必须逐源码实测**。本套件里两种结果都出现过：`t5`／`t6` 的归约程序用 `-O2` 编出来跑得通（17/17），而 `t7` 的 N 体程序在 `-O1`／`-O2` 下**必崩**（卡端 `/var/log/messages` 出现 `segfault at 0`，崩溃 ip 落在一条 `vpackstorelpd` 指令上），只有 `-O0` 跑通、且三档结果与宿主参考实现逐位一致 —— 所以 `t7` 固定用 `-O0`。
   **但不要拿「反汇编里有没有 `vpackstore`／`vscatter`」当判据**：用 SDK 里真正存在的 k1om objdump 数，跑得通的 N 体 `-O0` 产物有 **12 条**（全部写栈），崩溃的 `-O1`／`-O2` 各 **11 条**（其中一条改成寄存器寻址）。**条数不是判据，能不能在卡上跑通才是。**
3. **`COIBufferCreate` 在本移植链上不可用**（返回 `COI_OUT_OF_MEMORY(13)`），所以大数据一律走「宿主只传几十字节参数 ＋ 卡端按确定性公式自行生成」，结果经返回区回传。
4. **返回值要带一个确定性校验和**，宿主用自己的参考实现算同一个数比对 —— 这比「有没有结果」强得多，能同时抓住「算错」与「传错」。
6. **页数与段跨度必须按「哪一侧的页」解释**（这条是踩过两次的坑，完整链条见 `../docs/I-页大小对齐调查_CN.md` I.12）：宿主自身窗口的页数曾被按对端单位 ×4 打包，导致解映射多拆 4 倍页（`mic_smpt[i].ref_count < 0`），十一秒后 `micscif_get_dma_addr` 找不到地址直接 `BUG()`；而对端窗口的页数又被 ÷4，卡端「一段一页 4 KiB」整除成 0，同样让查找失败。修好之后：T8 用 1 MiB 窗口 ＋ 单次 26496 字节 RMA 稳定跑通 4 GiB。**此前「单次 RMA 超过 26496 字节就会卡死」的结论已撤回** —— 那是撞在已损坏的通路上；单次 RMA 的真实上限尚未标定。
5. 详细成因、正误写法对照与调试手段见 `../docs/OFFLOAD_GUIDE_CN.md`（约束在 §11.5 访问标志、§14 引用计数与状态、§15 大数据传输；排错在第六部分）。

## 八、常见卡壳

| 现象 | 原因 | 怎么办 |
|---|---|---|
| `scif_ok` 报 `/dev/mic/scif: Permission denied`（或前置状态里"设备可访问：否"） | 设备节点是 `crw------- root root`：上游 udev 规则写成旧式 `NAME="mic/%k"`，systemd-udev 忽略该写法，它的 `MODE="0666"` 落不到节点上 | `sudo bash tests/t1_install.sh`（装 `55-mic-perms.rules` 并立刻放开）；急用时 `sudo chmod 666 /dev/mic/scif /dev/mic/ctrl` |
| 重启后卡不在线、`/dev/mic` 不存在 | `mic` 模块没加载（被 `modprobe.d` 里的黑名单挡住，或 `modules-load.d` 未生效） | `lsmod \| grep mic` 确认；`sudo modprobe mic`；开机自动加载靠 `/etc/modules-load.d/mic.conf` |
| 卡在 `booting` 不动 | 卡端镜像没起来（`initramfs` 与 `bzImage` 不配套，或主机侧下发失败） | 看 `dmesg` 与卡端串口；重新 `echo boot:linux:<bzImage>:<initramfs> > /sys/class/mic/mic0/state` |
| 阶段报 `SKIP` | 本机缺该前置（工具链、依赖库、卡不可达等） | 按打印出来的提示设 `tests/config.sh` 或环境变量；跳过不等于通过 |
| `make` 报 `modules.order: Permission denied` | 源码树里有 `sudo make install` 留下的 root 属主产物 | `sudo rm -f Module.symvers modules.order`（或 `sudo chown -R $USER .`）；先 `make` 再 `sudo make install` 可减少复发 |

　　**用 `bash` 跑，不要用 `sh`**：脚本用了 bash 语法与特性，`sh` 在部分发行版上是 dash，会以各种奇怪的方式失败。

## 九、相关文档

| 想知道什么 | 读哪里 |
|---|---|
| 套件本身改了什么 | `CHANGELOG_CN.md`（英文版 `CHANGELOG.md`） |
| 真机验收结论与逐条判据 | `../docs/J-acceptance-tests_CN.md` |
| COI API 怎么用、程序怎么写 | `../docs/OFFLOAD_GUIDE_CN.md` |
| 被测对象的改动 | 发布树各包目录下的 `CHANGELOG_CN.md` |
| 总项目变更记录 | 项目根目录的 `CHANGELOG_CN.md`／`CHANGELOG.md` |
