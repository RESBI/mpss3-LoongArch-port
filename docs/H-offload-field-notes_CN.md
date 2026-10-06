# 附录 H　offload 实操记录：k1om 工具链、libgomp 与卡上 OpenMP

　　附录 F 讲的是 offload 的三条路线与判据，本附录记的是**实际动手做了什么、跑出了什么数**。全部步骤都在龙芯主机加卡的真机上完成，日志留在 `~/XeonPhiX100-LoongArch/offload-logs/`，可逐步重跑。

---

## H.1　目标与起点

　　目标是「用 k1om 自己的交叉编译器编译、链接 offload 相关的部分」。起点是三样现成的东西：

| 资产 | 位置 | 内容 |
|---|---|---|
| MPSS 的 k1om SDK | `mpss-sdk-k1om-3.8.6-1.x86_64.rpm`（198 MB） | k1om 的 `gcc`／`g++`／`as`／`ld`（GCC 5.1.1）、**1366 个头文件与 237 个库的 sysroot**、卡端 `libcoi_device`／`libmyo-service`／`libscif` |
| GCC 5.1.1-knc 源码 | `gcc-5.1.1-knc-master.zip`（158 MB） | k1om 后端补丁**加上**上游的 `liboffloadmic`／`intelmic-mkoffload`／`t-intelmic` offload 机制 |
| 卡与宿主 | 已就绪 | 卡 `online` 且 `coi_daemon` 在跑；宿主侧移植版 `libcoi_host` 已能枚举引擎（附录 F.1） |

## H.2　让 x86_64 的 k1om 编译器在龙芯上跑起来

　　SDK 里的编译器是 **x86_64 ELF**，龙芯上不能直接执行。宿主装有 **box64**，且 `binfmt_misc` 已注册，所以 x86_64 程序（包括编译器内部调用的 `cc1`／`as`／`ld`）能透明运行。落地时踩到并解决了三处：

| 现象 | 根因 | 解决 |
|---|---|---|
| 解出的目录里没有工具链，头文件数为 0 | `7z` 对 RPM 只解到内层 cpio 载荷 | 改用 **`bsdtar`**，一步解出 705 MB 完整树 |
| `cc1` 启动即报 `Loading needed libs in elf .../cc1` | `cc1` 依赖 SDK 自带的 `libmpc`／`libmpfr`／`libgmp` | `LD_LIBRARY_PATH` 指向 SDK 的 `x86_64-mpsssdk-linux/{lib,usr/lib}` |
| `as: unrecognized option '--march=k1om'` | SDK 内的 `as`／`ld` 是**指向 `/opt/mpss/...` 的绝对符号链接**，换目录即断，gcc 于是退回去调用系统的 LoongArch 汇编器 | 把树内 11 个绝对链接**改写为相对链接**（脚本 `fix_sdk_symlinks.py`），再用 `-B<SDK>/bin -B<libexec>` 指给 gcc |

　　固化后的调用方式（脚本 `k1om-cc` 即此内容，供 `configure`／`make` 直接使用）：

```bash
SROOT=~/XeonPhiX100-LoongArch/k1om-sdk/opt/mpss/3.8.6/sysroots/x86_64-mpsssdk-linux
SYSR=~/XeonPhiX100-LoongArch/k1om-sdk/opt/mpss/3.8.6/sysroots/k1om-mpss-linux
LD_LIBRARY_PATH=$SROOT/lib:$SROOT/usr/lib:$SROOT/usr/lib64 \
$SROOT/usr/bin/k1om-mpss-linux/k1om-mpss-linux-gcc \
  -B$SROOT/usr/bin/k1om-mpss-linux/ \
  -B$SROOT/usr/libexec/k1om-mpss-linux/gcc/k1om-mpss-linux/5.1.1/ \
  --sysroot=$SYSR ...
```

　　验证方式就是让它编一个程序并看 ELF 头：

```text
编译: [OK] 1896 字节    Type: REL  Machine: Intel K1OM
链接: [OK] 10888 字节   Type: EXEC Machine: Intel K1OM   解释器 ld-linux-k1om.so.2
```

## H.3　卡上第一个程序（自建工具链的第一次闭环）

　　把上面链接出来的可执行文件送上卡运行。传输上有一处小坑：卡上的 `sshd` 是 2019 年的 OpenSSH 7.4，与本机新版 `scp` 协商失败（`scp: Connection closed`），改用 `ssh ... 'cat > /tmp/文件'` 传输即可。卡上结果：

```text
/tmp/hello_k1om: ELF 64-bit LSB executable, Intel Xeon Phi coprocessor (k1om),
                 version 1 (SYSV), dynamically linked, for GNU/Linux 2.6.32, not stripped
hello from k1om, built on LoongArch via box64
退出码=0      卡上 uname: k1om / 2.6.38.8+mpss3.8.6
```

## H.4　自建 k1om 版 libgomp：MPSS 从未提供的那一件

　　卡上 OpenMP 与 offload 的卡端 worker 都依赖 k1om 的 `libgomp`，而 MPSS 的编译配置里写着 `--disable-libgomp`，SDK 与卡镜像里都**没有**它。用 GCC 5.1.1-knc 的 `libgomp` 源码单独构建：

- configure 以**交叉模式**运行（`--build=loongarch64…`、`--host=k1om-mpss-linux`），因此不试图运行测试程序；
- `CC` 指向前面那个 `k1om-cc` 包装；
- 需要补解 `libbacktrace`（libgomp 的硬依赖）；
- 文档目标 `stamp-build-info`（只跑 `makeinfo`）会因缺 `gpl_v3.texi` 等文件而失败，用 `make MAKEINFO=true` 让其空转即可，与库本身无关。

　　产物（已确认目标架构）：

```text
libgomp.so.1.0.0   720,422 字节   Machine: Intel K1OM   SONAME libgomp.so.1
libgomp.a        1,444,550 字节   omp.h 4,355 字节
NEEDED: libdl.so.2 libpthread.so.0 libc.so.6
GOMP_parallel 系列符号 27 个，OMP_* 入口 161 个
```

　　随后把它装进**编译器自己的运行时目录**（`-print-search-dirs` 给出的 install 目录与 `-print-file-name=crtbegin.o` 给出的 sysroot libdir 各放一份），使 `-fopenmp` 与 `#include <omp.h>` 无需任何 `-I`／`-L` 即开箱可用。

## H.5　卡上第一次真算：OpenMP 归约

　　测试程序是一个 2 亿项的并行归约（`#pragma omp parallel for reduction(+:sum)`），在龙芯上用 `-O2 -fopenmp` 编译，连同 `libgomp.so.1.0.0` 一起送上卡运行。实测：

| `OMP_NUM_THREADS` | 实际线程数 | 耗时 | 加速比 |
|---:|---:|---:|---:|
| 1 | 1 | 8.451 秒 | 1.0 |
| 61 | 61 | 0.233 秒 | **36.3 倍** |
| 244 | 244 | 0.282 秒 | 30.0 倍 |

　　`omp_get_num_procs()` 报 **244**（61 核 × 4 路 SMT），与卡的硬件一致。结果的正确性也可核对：程序输出 `66666666.166666656733`，而离散和 `(n-1)n(2n-1)/(6n²)`（`n = 2×10⁸`）的解析值为 `66666666.1666666667` —— 一致。61 线程时接近线性加速，244 线程（四路 SMT 全开）反而略慢，符合 KNC 顺序核的预期。

　　**这一步的意义**：卡上此前只有 Intel 自家的 ICC 能用的 OpenMP 能力，现在由「龙芯主机上、用 MPSS 的 k1om 编译器、自建 libgomp」这套组合实现，且全程可复现。

## H.6　卡端 offload worker：`offload_target_main` 已构建并在卡上加载成功

　　这是 offload 在卡侧的落点：由 `coi_daemon` 拉起、接收宿主送来的 offload 镜像并执行的 worker。用同一套 k1om 编译器构建，关键是**用树自己给出的目标侧参数**（`liboffloadmic/plugin/Makefile.am` 里那一行）：

```text
-DLINUX -DCOI_LIBRARY_VERSION=2 -DMYO_SUPPORT -DOFFLOAD_DEBUG=1 -DSEP_SUPPORT -DTIMING_SUPPORT -DHOST_LIBRARY=0
```

　　手工尝试时漏掉 `-DOFFLOAD_DEBUG=1` 与 `-DTIMING_SUPPORT`，结果 `OFFLOAD_DEBUG_TRACE`、`OffloadHostTimerData` 一类符号报错；改用官方参数后目标侧源文件全部编译通过。组成与结果：

| 部分 | 内容 |
|---|---|
| 目标专用源 | `runtime/coi/coi_server.cpp`、`compiler_if_target.cpp`、`offload_myo_target.cpp`、`offload_omp_target.cpp`、`offload_target.cpp`、`offload_timer_target.cpp` |
| 共用源 | `offload_common.cpp`、`offload_env.cpp`、`offload_table.cpp`、`offload_trace.cpp`、`offload_util.cpp`、`liboffload_error.c`、`liboffload_msg.c`、`ofldbegin.cpp`、`ofldend.cpp` 等（共 21 个对象归档为 `liboffloadmic_target.a`，159,444 字节） |
| 排除 | `*_host.cpp`、`cean_util.*`、`dv_util.*`、`offload_orsl.*`、`offload_engine.cpp`（宿主侧，链接不需要） |

　　链接产物：

```text
offload_target_main   90,073 字节   Type: EXEC   Machine: Intel K1OM
NEEDED: libcoi_device.so.0 libmyo-service.so.0 libgomp.so.1 libpthread.so.0 libdl.so.2
        librt.so.1 libstdc++.so.6 libm.so.6 libgcc_s.so.1 libc.so.6 ld-linux-k1om.so.2
.OffloadEntryTable. 段: 1 个
```

　　送上卡后依赖全部命中（`libcoi_device.so.0`、`libmyo-service.so.0`、`libscif.so.0`、`libstdc++.so.6` 卡上本就有，`libgomp.so.1` 是本次自建的），直接执行得到它自己的运行时报文：

```text
offload error: wait for process shutdown failed on device -1 (error code 1)
```

　　这句错误是 worker 被单独拉起（没有宿主 offload 运行时配合、参数不完整）时的正常反应，**它证明二进制在卡上成功加载、动态链接全部解析、并能进入自己的主流程**。至此 offload 的**卡侧组件全部就位**：`coi_daemon`、`libcoi_device`、自建 `libgomp`、`offload_target_main`。

## H.7　另一条 offload 路线：不需要 x86_64 的手写 COI

　　本节回答一个容易被上一节误导的问题：「那是不是没有 x86_64 就做不了 offload？」——**不是**。上一节讲的宿主侧三件套（带 offload 的宿主编译器、`liboffloadmic` 宿主库、`libgomp-plugin-intelmic`）属于**让编译器自动生成 offload 代码**那条路（附录 F 的路线 B），它确实卡在 x86_64 与 GCC 版本上。而 offload 的**运行时模型**本身并不需要它们：

　　COI 才是 offload 的落地接口（Intel 自家的 `micnativeloadex` 也是走 COI）。宿主用 COI 在卡上创建进程、把数据放进共享缓冲、调用卡端导出的函数——这就是 offload。我们手上已经具备全部条件：

| 部件 | 状态 |
|---|---|
| 卡端 `coi_daemon` | 镜像自带，开机自启 |
| 卡端 `libcoi_device.so.0` | 镜像自带 |
| 卡端 OpenMP 运行时 | **本次自建**（H.4） |
| 卡端 offload 程序 | **本次自建**：用 k1om 编译器编出链了 `libcoi_device` 的程序（H.3 打通的就是这条编译路径） |
| 宿主侧 COI | **移植版 `libcoi_host`**，已在龙芯上验证引擎枚举与句柄（F.1） |

　　于是演示做成了这样两半：

- **卡端 `offload_sink.cpp`**：按 COI 教程的骨架写（`COIPipelineStartExecutingRunFunctions()` + `COIProcessWaitForShutdown()`），导出一个 `COINATIVELIBEXPORT` 的 `CardReduce()` 函数，**函数体内用 `#pragma omp parallel for` 做 2 亿项归约**，把结果、卡的硬件线程数、实际线程数、卡上耗时写回返回区。实测编译产物 13,117 字节、`Machine: Intel K1OM`、`NEEDED` 含 `libcoi_device.so.0` 与 `libgomp.so.1`（自建）、导出 `CardReduce` 符号。
- **宿主侧 `offload_host.cpp`**：在龙芯上编译（`Machine: LoongArch`），流程为枚举引擎 → `COIProcessCreateFromFile("/tmp/offload_sink")` → `COIPipelineCreate` → `COIProcessGetFunctionHandles("CardReduce")` → `COIPipelineRunFunction`（传计算规模、收回结果）→ 与解析值比对 → 销毁管道与进程。

　　途中修掉一处自己写的错：`COIPipelineRunFunction` 有 **12 个参数**（除缓冲与访问标志外，还有依赖事件数与依赖数组），漏参数会导致类型不匹配的编译错误；原型以树里的头文件为准。

　　这条路线**完全在龙芯与卡之间完成，不需要任何 x86_64 环节**，而且卡端函数内部照常是 OpenMP 并行。它与路线 A（卡上原生运行）的区别只在于「谁发起、数据怎么过去」：路线 A 是人登卡运行，路线 C 是宿主程序发起并把结果取回。

## H.8　编译器生成型 offload（路线 B）剩余的一块：宿主侧运行时

　　宿主侧需要三样东西：带 offload 的宿主编译器、`liboffloadmic` 的宿主库、以及 `libgomp-plugin-intelmic.so.1`。它们必须建立在 **x86_64** 环境里，原因与附录 F.3.1 相同（`liboffloadmic/configure.tgt` 拒绝非 x86 宿主；GCC 5.1.1 无法以 loongarch64 为宿主；offload 要求宿主与目标编译器同源同版本以便交换 LTO 中间码）。可行做法两种：

1. **在龙芯上用 box64 跑一个 x86_64 用户态环境**，在其中构建 GCC 5.1.1 的「宿主 + k1om offload」两套编译器与宿主运行时。全部靠模拟，速度慢，但不需要额外机器。
2. **使用一台 x86_64 Linux 机器**（或虚拟机）完成同样的构建，再把产物与卡对接。

　　值得注意的是：**目标侧编译器不必重建**。SDK 自带的 k1om GCC 就是 5.1.1，且带 `lto1` 与 `liblto_plugin.so`，与将要构建的宿主编译器同版本，满足 LTO 版本一致的要求；配合本附录构建的目标侧 `libgomp` 与 worker，卡侧已经没有缺口。

　　在宿主侧完成之前，龙芯与卡之间已经可用的通路是：管理面的 SCIF（`micctrl`／`mpssd`）、宿主侧 COI 的引擎与句柄（附录 F.1）、以及本次打通的「k1om 编译 → 送卡 → 原生运行／OpenMP」链路。

## H.9　offload 实跑记录：两处根因，以及卡在哪一步

　　H.7 的路线（手写 COI）端到端搭起来之后，真正的 offload 调用仍然失败。这一节记的是从「失败」到「根因」的完整排查链，以及当前还剩什么。

### H.9.1　起点：一个笼统的错误码

　　宿主程序枚举引擎、取句柄都成功，但 `COIProcessCreateFromFile` 只返回 `COI_ERROR(1)`（未指明）✓。逐层剥下去：

| 手段 | 得到的信息 |
|---|---|
| 参数对照实验（改 proxy、库搜索路径） | 给库路径时错误变成 `COI_BINARY_AND_HARDWARE_MISMATCH(22)`，不给则是 `COI_ERROR(1)` —— 22 是「依赖库机器类型不符」 |
| 读 COI 源码 `shared_library_finder.cpp:235` | 宿主会把 sink 的**每个依赖**读出来校验 ELF Machine，全部必须是 `EM_K1OM`；据此建了宿主侧的 k1om 依赖目录（7 个依赖全部为 `Intel K1OM`） |
| 重新实验 | 22 消失，回到 `COI_ERROR(1)` —— 于是失败点在依赖校验之后 |
| `strace` | 失败发生在 SCIF 的 `SCIF_REG`（8 号）上：`ioctl(3, _IOC(READ\|WRITE, 0x73, 0x8, 0x8), …) = -1 EINVAL` |
| 驱动 `pr_debug`（`module mic +p`） | `SCIFAPI register: ep … Connected len 0x1000 offset 0x0 prot 0x3 map_flags 0x0` 紧随 `scif_register err -22` —— **入参全部合法却被拒** |

### H.9.2　根因一：4 KiB 的注册粒度撞上 16 KiB 的内核页

　　驱动那句静默 `return -EINVAL` 来自 `__scif_register` 的页对齐检查：

```c
if ((!len) || (align_low((uint64_t)addr, PAGE_SIZE) != (uint64_t)addr) ||
              (align_low((uint64_t)len,  PAGE_SIZE) != (uint64_t)len))
        return -EINVAL;
```

　　本机内核是 `7.1.13-aosc-main-16k`、`PAGE_SIZE = 16384`，而 MPSS 的 SCIF 用户态按 **4096** 注册（`len 0x1000`）—— `4096` 不是 16384 的整数倍，于是被静默拒绝。用一个运行时把 `len` 向上取整到实际页大小的拦截器验证：对照组仍 `errno=22`，实验组 `len 0x4000` 顺利通过检查。**根因确认**。

### H.9.3　根因二：packed 结构体里的自旋锁

　　过了对齐检查后，内核立刻报异常，进程变僵尸。用 `strace` 的现场与内核报告交叉定位：

```text
Unhandled kernel unaligned access[#1]:
  ERA: _raw_spin_lock_irqsave+0x44/0xf0      ← 出错的是内核自旋锁本身
  BADV: 90000001c7fd8096                      ← 非 4 字节对齐的地址
Call Trace:
  _raw_spin_lock_irqsave → prepare_to_wait_event → micscif_prep_remote_window [mic]
```

　　即 `micscif_prep_remote_window` 等待的队列里，`spinlock_t` 落在非对齐地址上。原因在 `include/mic/micscif_rma.h`：`struct reg_range_t`（内含 `wait_queue_head_t`）被标了 `__attribute__ ((packed))`。x86 容忍非对齐锁（只是慢），LoongArch 内核态直接 `die`。全模块扫描确认「打包且含锁/等待队列」的结构体**只有这一个**，去掉该属性后 `unaligned` 计数归零、函数体由 `0x8f0` 缩到 `0x520`。**这一处是真正的移植缺陷**，已作为补丁进发布版源码树。

### H.9.4　当前卡在哪：页大小假设贯穿 RMA 层

　　packed 修掉后，注册能走到更深处，但随即出现另一类异常：

```text
ESTAT: 00480000 [ADEM]   BADV: e0000e4800000000
ERA: micscif_prep_remote_window+0x198   （源码行 micscif_rma.c:1161）
Code: … <380c3b7e> …    ← ldx.d  $s7, $s4, $t2
```

　　`0xe000…` 是 LoongArch 的非缓存/物理窗口地址，说明一个 `scif_ioremap` 的结果是坏的。顺着读下去，问题出在**页数语义**：宿主侧 `window->num_pages[j] << PAGE_SHIFT`、`nr_pages = len >> PAGE_SHIFT` 全部以**宿主页**为单位，而卡端是 4 KiB 页。宿主算出 `nr_pages = 1`（16 KiB 页），卡端理解成 4 KiB —— 卡片只映射 4 KiB，宿主却 pin 了 16 KiB，返回的窗口描述与实际不符，宿主于是拿到坏地址。

　　这与 H.9.2 是同一个根源的两面：**MPSS 的 SCIF 把「宿主页」当成了协议单位**。在 x86 上宿主与卡都是 4 KiB，所以从不暴露；在 16 KiB 页的龙芯上处处暴露。要改就得在**协议单位（4 KiB）与宿主页（16 KiB）之间做转换**，而 `PAGE_SHIFT` 在该层出现在十几处（`micscif_map.h:44/201-211`、`micscif_rma.h:666/675/717/733/756-761`、`micscif_rma.c:557` 等），且 DMA 映射层（`mic_map`／`dma_map_page`）有自己的页假设。

### H.9.5　两条出路

| 路线 | 内容 | 代价与风险 |
|---|---|---|
| **改用 4 KiB 页的内核** | MPSS 的页假设自然成立，SCIF/COI/offload 按其设计工作 | 需要为龙芯构建一个 4 KiB 页内核（LoongArch 支持 4/16/64 KiB 页）；与现有 16 KiB 系统切换；风险集中在内核构建与启动，链条其余部分不动 |
| **深度改造宿主 SCIF 的 RMA 层** | 协议单位固定 4 KiB、pin 按宿主页、物理地址表按 4 KiB 展开 | 牵动十几处且每处都可能引出新的 ALE/ADEM；DMA 层也要一并核对；属于小时级的迭代工作 |

　　无论走哪条，本轮已经拿到的成果不受影响：MPSS 工具端在龙芯上的完整移植、k1om 交叉编译器在龙芯上可用、自建 k1om `libgomp`、卡上原生 OpenMP（61 线程 36 倍加速）、卡端 `offload_target_main`，以及本附录记下的这两处根因。

### H.9.6　更正：区分「真实缺陷」与「诊断手段的副作用」

　　上面 H.9.4 把 `ADEM` 归因于「MPSS 的协议单位纠缠在 RMA 层」。核对地址形态后，这个结论**下得过重**，应当收窄：

| 现象 | 出错地址 | 性质 |
|---|---|---|
| packed 引起的 `ALE` | `页对齐基址 + 150`（150 ≡ 2 mod 4，且是编译期成员偏移，packed 版反汇编里正是硬编码 `+150`） | **真实缺陷**：与运行期页大小无关 |
| 页数不一致引起的 `ADEM` | `0xe0000e4800000000`（LoongArch 物理/非缓存窗口） | **很可能是诊断手段的产物** |

　　理由：H.9.2 里为了越过驱动的页对齐检查，用一个 `LD_PRELOAD` 拦截器把注册长度从 4096 抬到 16384。这等于对驱动**谎报长度** —— 宿主按 16 KiB 页算出 `nr_pages = 1`，而这一页在协议上与卡端的 4 KiB 页不等价，于是窗口描述与实际不符，`scif_ioremap` 得到坏地址。换成「让驱动接受 4 KiB 并把换算做对」的正规修法，`ADEM` 未必出现。

　　因此**已被证实的原始缺陷只有一个**：驱动在 16 KiB 页内核上按 `PAGE_SIZE` 校验页对齐，从而静默拒绝 MPSS 用户态固定使用的 4096 字节注册。RMA 层里 `PAGE_SHIFT` 的广泛使用是**待评估**的风险点，而不是已证实的第二个缺陷。

　　同样需要如实标注的两处：

- **关闭 PC3/PC6 属于未验证的假设**：它是为解释 `micscif_nodeqp_send … error -19` 而做的改动，但关掉之后 `-19` 依旧出现，说明省电态不是该错误的原因。保留它只应作为一项显式配置选择，不能算作修复。
- **`COIEngineGetInfo` 的结构体尺寸不匹配仍未解释**：传 5200 字节得 `COI_ERROR(1)`，其他尺寸得 `COI_SIZE_MISMATCH(12)`，说明移植出的头文件与库对该结构体的认知不同。它与页大小无关，是独立的遗留问题。

## H.10　页大小对齐：依赖面调查与修改方案

　　H.9 已经把现象定到「宿主 16 KiB 页 vs MPSS 假定的 4 KiB」。这一节把依赖面查清，并列出可选修法。

### H.10.1　关键结构：两端同源，协议里的「页」是发送方自己的 PAGE_SIZE

　　MPSS 的内核模块是**一棵树编两端**：`Kbuild` 依据 `MIC_CARD_ARCH` 决定编宿主还是卡端（空值＝卡端）。也就是说宿主侧与卡端的 SCIF **是同一份源码**，而源码里的「页」一律写成 `PAGE_SIZE`／`PAGE_SHIFT`。在 x86 上两端都是 4 KiB，协议自然对齐；一端是 16 KiB 时，同一个字段在两端含义不同 —— 这才是 H.9.4 那个 `ADEM` 的结构性原因。

### H.10.2　各层的页大小依赖

| 层 | 单位 | 证据 |
|---|---|---|
| COI 用户态 | **4096 硬编码** | `_MemoryRegion.h:71 #define PAGE_SIZE (4096)`、`_Message.h:76 #define COI_PAGE_SIZE 4096` |
| libscif | 透传，不含页常量 | `scif_api.c` 只把调用方的 `len` 填进 ioctl 结构体 |
| 宿主驱动（与卡端同源） | `PAGE_SIZE`（此处 16 KiB） | 对齐检查 `micscif_api.c:1940/2251/2526/2662`；计数 `1946/2255/2558/2566/2674`；偏移换算 `micscif_rma.h:44/733/757`；DMA 映射 `micscif_map.h:201-211` |
| 宿主 DMA／SMPT 层 | **固定常量**，与本问题无关 | `micscif_smpt.h:79 #define MIC_SYSTEM_PAGE_SHIFT 34`、`micbaseaddressdefine.h:103 MIC_SYSTEM_PAGE_SIZE 0x0400000000` |
| 卡端 | 同源，但其内核 `PAGE_SHIFT = 12` | `Kbuild` 的 `MIC_CARD_ARCH` 分支 |

　　DMA／SMPT 层用固定常量这一点很重要：**要改的只有 `micscif` 的记账**，DMA 描述符层不必动。

### H.10.3　四种修法与代价

**方案 1：宿主驱动改为「协议 4 KiB 单位 + 宿主页 pin」**

- 把**跨到卡端**的量改用固定 4 KiB 单位：窗口的 `nr_pages`／offset／len、`num_pages[]`、`micscif_get_dma_addr`／`get_phys_addr` 的 offset→page 换算；
- 宿主侧的内存管理仍按宿主页（`ALIGN(len, PAGE_SIZE)` 后再 pin）；
- 规模约 10 至 15 处机械改动；**卡端与用户态零改动**；
- 风险中等：点较多，但每处都有明确的单位对照，可逐处验证。

**方案 2：用户态按宿主页注册 + 驱动只改协议边界**

- 用户态把 `_MemoryRegion.h` 的 `PAGE_SIZE` 换成 `sysconf(_SC_PAGESIZE)`（**不能动** `_Message.h` 的 `COI_PAGE_SIZE`，那是报文分帧）；
- 驱动仍须把协议量折算成 4 KiB，否则就是 H.9.3 里那个「谎报长度」的结局；
- 规模较小（用户态 1 处 + 驱动 4 至 6 处），但会动到 COI 自己的缓冲区偏移假设，需要额外验证。

**方案 3：改用 4 KiB 页宿主内核**

- 龙芯内核支持 4／16／64 KiB 页，构建一个 `CONFIG_PAGE_SIZE_4KB` 的内核即可，**MPSS 代码一行不改**；
- 一次性让**所有** 4 KiB 假设成立 —— 不只是注册路径，还有尚未触及的 `scif_mmap`（`vm_pgoff << PAGE_SHIFT`，`micscif_api.c:2887/2941`）与 `readfrom/writeto` 的分块（`MAX_PAGE_ORDER + PAGE_SHIFT`，同文件 `1646/1711`）；
- 代价是构建与切换内核（约一小时），模块需按新内核重编一次；风险集中在内核构建与启动。

**方案 4：把卡端改成 16 KiB 协议单位** —— 需重编卡端内核与镜像，改动线协议，而卡端自身页仍是 4 KiB，收益与代价不成比例，不取。

**方案 5：绕开 SCIF 内存注册** —— COI 的第一个注册（信号页 4096 字节）就失败，无法绕开，不取。

### H.10.4　建议的顺序：先 3 后 1

　　方案 3 的路径上没有未知数，且有独立价值：它能**验证「页大小就是唯一障碍」这一判断本身**。若在 4 KiB 页内核上 offload 直接跑通，则诊断闭环；随后若要支持 16 KiB 系统，再按方案 1 改造，且此时手上已有一个可对照的可用基线，调试成本会低得多。反过来先做方案 1，每轮迭代都要在「新引入的 bug」与「还没改完」之间做判断，成本高且不易收敛。

　　无论走哪条，**H.9.3 的 packed 修复都是必需的**（它与页大小无关），且该修复已在发布版源码树中。

　　页大小对齐的完整调查（现象判别法、逐层依赖面、换算点清单与五种修法的取舍）另见[附录 I](I-page-size-alignment_CN.md)。

　　**后续实测（见 H.13）**：方案 1 在注册与拷贝两条路径上落地之后，**16 KiB 页宿主上 offload 已端到端跑通**，因此不必再换 4 KiB 页内核 —— 本节「先 3 后 1」是当时的建议，已被实际结果取代。

## H.11　复现清单

| 步骤 | 脚本（在 `~/XeonPhiX100-LoongArch/`） | 日志（在 `offload-logs/`） |
|---|---|---|
| 解 SDK、跑通 k1om 编译器 | `off_11_sdk2.sh`、`fix_sdk_symlinks.py` | `sdk-*.log` |
| 编出并上卡运行第一个程序 | `off_13_build.sh`、`off_15_run.sh`、`off_16_cardrun.sh` | `k1om-run-*.log`、`card-run-*.log` |
| 构建并安装 k1om libgomp | `off_21_libgomp.sh`、`off_22_libgomp.sh`、`off_24_libgomp_fin.sh`、`off_32_ompfinal.sh` | `libgomp*.log`、`omp-final-*.log` |
| 卡上 OpenMP 实测 | `off_32_ompfinal.sh` | `omp-final-*.log` |
| 手写 COI offload 演示 | `off_60_demo.sh`、`off_61_run.sh` | `offload-demo-*.log`、`offload-run-*.log` |
| 构建卡端 offload worker | `off_43_target2.sh`、`off_44_target3.sh`、`off_45_workercard.sh` | `offld-target*.log`、`worker-card-*.log` |
| 端到端验收（T0–T8） | 发布树 `tests/run_all.sh`、`tests/run_tests.sh`、`tests/t7_nbody.sh` | `tests/logs/run-<时间戳>/`（见[附录 J](J-acceptance-tests_CN.md)） |

　　工具链包装脚本：`k1om-cc`（同时充当 `k1om-cxx`）；自建运行时暫存前缀：`k1om-sysroot-extra/`。

## H.12　与附录 I 的对应：驱动层已闭环

　　本轮围绕 offload 的排查最终收敛为两条独立缺陷，其中与页大小有关的部分见[附录 I](I-page-size-alignment_CN.md)的 I.10 与 I.11。这里只记结论与当前状态：

| 项 | 状态 |
|---|---|
| 宿主页 16 KiB 导致的注册被拒 | **已修**（长度按协议页校验 + 送出侧换算），实测分配请求携带 `nr_pages=4` |
| `packed` 结构内嵌等待队列导致的非对齐原子访问 | **已修**（四个等待队列指针化），实测 `unaligned` 计数归零 |
| 两端结构体布局不一致 | **已修**（恢复 `packed`），实测描述区 `magic` 等于 `SCIFEP_MAGIC` |
| 宿主到卡的批量写 | **已验证逐字节正确**（卡端校验 4096 字节不符 0 个） |
| `COIProcessCreateFromFile` 阻塞 | **已解决**（H.13）；原判「属 COI 应用层、与页大小和结构体布局无关」需修正为：它正是由协议页数与 sink 缺 `-rdynamic` 两件事引起的 |

　　用于验证的两个小程序（卡端 `rma_srv`、宿主 `rma_cli`）与相关脚本一并留在服务器的工作目录下，脚本名以 `rma_` 开头。

## H.13　补记：COI 进程创建已打通（三处修复与实测数字）

　　H.12 里那个未决项已经解决。它最终不是「COI 应用层」的问题，而是三处具体缺陷：

| # | 缺陷 | 修法 | 判据 |
|---|---|---|---|
| 1 | 26496 字节的创建命令在 RMA 拷贝路径上按页步长算错，只有第一页正确 | 拷贝路径按协议页／宿主页换算（[附录 I](I-page-size-alignment_CN.md) I.10） | `t4` 七档长度扫描加 26496 全量校验，0 个字节不符 |
| 2 | 对端窗口长度按宿主页计算 | 送出前按 $P_h / P_c$ 换算 | 内核日志出现 `nr_pages=4`（宿主 1 个 16 KiB 页 ＝ 对端 4 个 4 KiB 页） |
| 3 | 卡端 sink 未加 `-rdynamic`，导出符号不在 `.dynsym` | 链接时加 `-rdynamic` | `readelf --dyn-syms` 可见 `CardReduce`；否则宿主收到 `COI_DOES_NOT_EXIST(5)` |

　　三处修掉之后，端到端路径逐项走通（`t5`，17 项全过）：

| 步骤 | 实测 |
|---|---|
| 枚举引擎／取句柄 | `1) 引擎数 = 1`、`2) 取到引擎 0 句柄` |
| 在卡上创建进程 | `COI_SUCCESS(0)`（此前是 `COI_ERROR(1)` 或 `COI_BINARY_AND_HARDWARE_MISMATCH(22)`） |
| 建管道、按名取函数 | `COIPipelineCreate` 成功，`CardReduce` 取到 |
| 卡上计算 | 2×10⁸ 项 OpenMP 归约，**240 线程**，相对误差 −3.35e-16 |
| 收尾 | 管道与进程正常销毁，卡端 `coi_daemon` 仍在 |
| 内核侧 | 非对齐异常 0、内核异常 0 |

　　随后用同一套机制做了「重计算 ＋ 大数据量」的示例（N 体引力 $O(N^2)$，三档规模）：初始条件由卡端按确定性公式自行生成，结果经返回区回传，宿主用自己的参考实现算同一个校验和比对。

| 档 | N | 步数 | 线程 | 校验和比对 | 能量相对误差 | 卡上耗时 | 算力 |
|---|---:|---:|---:|---|---:|---:|---:|
| 1 | 1024 | 20 | 240 | **差 0.000e+00** | 1.96e-04 | 0.40 s | 0.62 GFLOPS |
| 2 | 8192 | 10 | 240 | —（规模大，只查能量守恒） | 8.47e-05 | 3.1 s | 2.10 GFLOPS |
| 3 | 16384 | 5 | 240 | — | 4.02e-05 | 5.1 s | 2.68 GFLOPS |

　　仍有两处限制，都由实测得出，也已写进随发布版交付的指南：

| 限制 | 现象 | 现用替代 |
|---|---|---|
| `COIBufferCreate` 不可用 | 返回 `COI_OUT_OF_MEMORY(13)`，无论用宿主内存、页对齐内存还是 `NULL`（由库自分配） | 入参走 `miscData`（≤64 KiB）、结果走返回区（实测 64 字节可靠） |
| `COIEngineGetInfo` 结构体尺寸不匹配 | 传 5200 字节得 `COI_ERROR(1)`，其他尺寸得 `COI_SIZE_MISMATCH(12)`（F.1 已记） | 不使用该接口 —— 枚举、建进程与取句柄都不需要它 |

　　「卡端 sink 该怎么写、哪些写法会在卡上崩」的完整清单，见随发布版交付的《KNC offload 编程手册》（`OFFLOAD_GUIDE_CN.md`，按 COI API 逐条编排）；把它跑一遍的验收脚本见[附录 J](J-acceptance-tests_CN.md)。

## H.14　更正：掩码压缩存储不是判据（一条自查规则的否证）

　　本节否证一条**本报告自己早期给出过的规则**。它原是：卡端程序崩溃是因为工具链生成了**掩码压缩存储指令**（`vpackstorelpd`），所以自查法就是「`objdump -d | grep -c vpackstore` 必须为 0」。这条规则进过指南，也进过本附录的早期结论。2026-10-06 复核后它不成立。

　　起因是 N 体示例在 `-O0` 下跑通、在 `-O1`／`-O2` 下必崩（卡端日志 `segfault at 0 ip … in nbody_sink`）。把三个优化档都编出来，用**确实存在的**那个 k1om objdump（`sysroots/x86_64-mpsssdk-linux/usr/bin/k1om-mpss-linux/k1om-mpss-linux-objdump`）逐个数：

| 二进制 | `vpackstore`／`vscatter` | 结果 |
|---|---:|---|
| N 体 sink（`-O0`） | **12 条**（全部写栈，如 `-0x90(%rbp)`） | **跑通**，校验和与宿主参考逐位一致 |
| N 体 sink（`-O1`） | 11 条（其中一条写 `0x10(%r12)`） | 崩，崩溃 ip 正是那一条 |
| N 体 sink（`-O2`） | 11 条（同上） | 崩，崩溃 ip 正是那一条 |
| 归约 sink（`-O2`，H.7 的示例） | 3 条 | 跑通 |

　　三条判据同时成立，规则因此被否证：

1. **该指令本身是合法的 KNC 指令** —— KNC 有自己的 512 位向量寄存器与 16 位掩码寄存器，`vpackstorelpd` 就在它的 ISA 里；
2. **条数与成败无关** —— 跑得通的产物有 12 条，崩掉的有 11 条；
3. **早先那个「`-O0` 为 0 条」是假通过** —— 它出自一条并不存在的 objdump 路径（`sysroots/k1om-mpss-linux/usr/bin/…`），命令失败后 `grep -c` 照样返回 0。

　　现在的规则只剩一条，而且它是实测的、**逐源码**的：**优化档必须自己跑一遍再定。** 本套件里的两个样本正好相反 —— N 体 sink 在 `-O1`／`-O2` 下必崩、只能 `-O0`（本节上表），而归约 sink 用 `-O2` 编出来 17 项全过。成因仍未定论（现象是 GCC 5.1.1 的 k1om 后端在某种写法下把该指令用在寄存器寻址上并当场崩）。

　　方法论上有一条值得留给后来者：**自查规则自己也要有对照样本。** 当时手里明明有一个「已知可用」的样本（H.7 的归约 sink，`-O2` 编的、17 项全过），只要把它一起量，就会立刻看到 3 条，而不至于把「相关」当成「因果」，也不会让一条错规则活到写进指南之后。
