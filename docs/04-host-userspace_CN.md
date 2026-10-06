# 第四章　主机用户态：一层几乎不含 x86 的 C 代码

> 　本章的对象是 MPSS 装在主机上的**用户态**那一半：`mpssd`、`micctrl`、`libmpssconfig`、`libscif`、`micinfo`/`micflash` 等。
> 　与内核模块不同，这一层的问题不是「能不能编」，而是「编完之后还缺什么」。
> 　上一章是 [第三章　MPSS 内核模块解剖](03-mpss-kernel-module_CN.md)，下一章是 [第五章　x86 耦合审计](05-x86-coupling-audit_CN.md)。
> 　本章的行数与条目统计来自对交付的 98 个安装包逐个解包清点（清单产物见 `_work/findings/rpm-contents/`）。

---

## 4.1　这一层到底交付了什么

　　MPSS 3.8.6 的 Linux 交付物是 **98 个 RPM**，散在若干子目录里：

| 位置 | 内容 | 个数 |
|---|---|---|
| 根目录 | 主机用户态二进制包主体 | 48 |
| `dbg/` | DebugInfo，**内嵌了源码** | 10 |
| `modules/` | 内核模块（第三章的对象） | 22 |
| `ofed/` | OFED / DAPL / libibscif / ibpd | 8 |
| `src/` | srpm | 4 |
| `perf/` | `micperf` | 2 |
| `psm/` | PSM | 2 |
| `ganglia/`、`relmon/` | 各一 | 2 |
| **合计** | | **98** |

　　这批包里的 ELF 绝大多数属于两类：**x86-64** 共 349 个，**k1om**（ELF `e_machine` = 181）共 250 个（另有 4 个别的架构，SPARCv9／SPARC／PowerPC／i386 各 1 个，记录在 `_work/findings/_inv_elf.txt` 的架构统计一节）。后者是卡上跑的，本次一概不动 —— 这也是为什么交付里有一份 27,363 个文件、659,630,329 B 的 k1om SDK（预编译交叉工具链加卡侧 sysroot），它们与龙芯无关。

　　**不是所有东西都有源码**，这一点必须先分清，否则工作量会算错。有源码的是主机用户态主体；没源码的是一批 GUI、诊断、RAS、PSM 组件，4.7 节逐个列。

---

## 4.2　有源码的部分

　　随包交付了一份源码归档目录 `mpss-src-3.8.6/…/src/`，共 **28 个 `.tar.bz2`**。其中主机用户态**真正需要重新编译**的是这几个：

| 归档 | 规模 | 产物 | 移植价值 |
|---|---|---|---|
| `mpss-daemon-3.8.6.tar.bz2` | 111.5 KB | `/usr/sbin/mpssd`（1,564 行）、`/usr/sbin/micctrl`（17,395 行）、`libmpssconfig.so.0.0.1`（7,306 行）、`micctrl_passwd`（493 行） | ★★★★★ |
| `libscif-3.8.6.tar.bz2` | 32.2 KB | `libscif.so.0.0.1`（**385 行**）、`/usr/include/scif.h`（1,561 行头文件） | ★★★★★ |
| `mpss-micmgmt-3.8.6.tar.bz2` | 8.63 MB | `micinfo`/`micflash`/`flash1`/`mpssflash`/`micsmc`、`libmicmgmt.so.0.0.2`（21,488 行） | ★★★★★ |
| `mpss-coi-3.8.6.tar.bz2` | 550.8 KB | COI 卸载框架，C++ 65,174 行 | ★★★★ |
| `mpss-myo-3.8.6.tar.bz2` | 484.5 KB | MYO 卸载/共享内存抽象，32,570 行 | ★★★★ |
| `micperf-3.8.6.tar.bz2` | 308.2 KB | 性能测试，5,312 行 | ★★ |

　　合计 **152,913 行**。逐归档是 26,423（`mpss-daemon`）＋ 1,946（`libscif`：385 行 `.c` 加 1,561 行 `.h`）＋ 21,488（`mpss-micmgmt`）＋ 65,174（`mpss-coi`）＋ 32,570（`mpss-myo`）＋ 5,312（`micperf`）。计数口径：六个归档解包树里 `.c`／`.h`／`.cpp` 三种文件的**全部行数**，含空行与注释，逐文件 `ReadAllLines` 实测、可逐条复算（命令见附录 B 的 §B.5 第五条）。不含 `mpss-miccheck`，也不含 `micperf` 里那几个 Python 脚本和全部 man／UserGuide 文本。

　　这里有一件容易被规模吓到、但实际很轻的事：**内核模块那一层只有 `mic.ko` 一个模块**；用户态这一层虽然行数多，但全部是普通 C/C++，不含任何特权代码、不含任何内核接口绑定，换架构就是换编译器。

---

## 4.3　x86 耦合在用户态几乎不存在

　　这是本章最省事的结论。把整个用户态源码树按「是否出现 x86 特有构造」逐条扫描，命中如下：

| 构造 | 命中位置 | 说明 |
|---|---|---|
| `__x86_64__` 条件编译 | `mpss-myo/src/include/myobasictypes.h:53`、`mpss-coi/src/api/sysinfo/sysinfo_common.cpp:64,88`、`micperf/gemm/bench.c:89,108` | 共 10 处，全部是「如果是 x86 就用某种方式打印/取时间」，属改写级 |
| x86 SIMD 内在函数 | `mpss-myo/src/consistent/myodiff.c:75,127,128,278`（SSE2）、`micperf/gemm/utils.c:31,107,118`（`immintrin.h`） | **只有两个文件**，是用户态唯一的真指令集耦合 |
| 硬编码的 x86 路径 | **零命中** | 硬编码的全是发行版路径与外部命令，与处理器无关 |
| `#include <asm/…>` | **零命中** | 用户态不含内核头 |
| `arch/x86` 引用 | **零命中** | — |

　　那两处 SSE2 的处理办法不是移植，而是替换：`myodiff.c` 用 SSE2 做一致性差分，龙芯的 LSX/LASX 是另一套内在函数，写法完全不同；`micperf` 是性能测试工具，可以直接放弃或改成标量实现。两者都不在主链路（用户程序调用 SCIF/COI/MYO 的那条路）上。

　　需要留意的是「这层代码依赖哪些外部命令」。见 4.5。

---

## 4.4　主机与内核之间的接口，本身不含任何处理器相关内容

　　这一条很重要，因为它是主机用户态能被大量复用的全部理由。

　　主机用户态与 `mic.ko` 之间的契约只有三种形态：

| 契约 | 具体形式 | 在哪定义 | 与处理器有关吗 |
|---|---|---|---|
| sysfs 文本属性 | `/sys/class/mic/micN/*`，逐行文本读写 | `host/linsysfs.c`、`micscif/micscif_sysfs.c` | 无关 |
| `/proc` 文件 | `/proc/mic_ramoops`、`/proc/mic_vmcore` | `host/uos_download.c`、`host/vmcore.c` | 无关 |
| 字符设备 + `ioctl` | `/dev/mic/*`，`libscif.so.0` 是它的一层薄封装 | `host/linux.c`、`include/scif_ioctl.h` | 无关 |

　　`libscif` 尤其能说明问题：整个库 **385 行**，做的事就是打开 `/dev/mic/scif` 然后发 `ioctl`。它不含任何架构假设，因为它根本没有资格含 —— 它只是 `ioctl` 的参数打包器。

　　这条契约在移植中是**必须冻结**的部分，逐项清单见 [A 接口契约清单](A-interface-contracts_CN.md) 的 A.7 与 A.8。

　　唯一一处看着像 x86 检查、实际**必须保留**的代码：`mpss-daemon/libmpssconfig/verify_bzimage.c` 会在把内核镜像推给卡之前校验它是 k1om 的 —— 检查 `0x55aa`@510、`"HdrS"`@514、字节 529 等于 1、`0x1f8b`@530、ELF `e_machine` 等于 `0x3e` 或 `0xb5`。这里的 `0x3e` 是 x86-64、`0xb5` 是 `EM_K1OM`，**检查的对象是卡的内核，不是主机**。移植到龙芯之后这段校验一个字都不用改，反而是防止推错镜像的安全网。

---

## 4.5　真正的工作量：外部命令和它们在新平台上的下场

　　用户态这一层的绝大部分改动落在这里。逐个：

| 外部命令 / 路径 | 出现位置 | 现在的身份 | 在龙芯新世界上的下场 |
|---|---|---|---|
| `/usr/sbin/brctl` 或 `/sbin/brctl` | `micctrl/network.c:113-145`（路径表）、`:4031,4061,4090,4119` | 建 `micbr0` 网桥 | **`brctl` 在新内核上已废弃** → 必须改成 `ip link add name micbr0 type bridge` 一类 |
| `/sbin/ifconfig` | `micctrl/network.c:4001` | 读主机网卡状态 | 改成 `ip link` 并自己解析 |
| `/sbin/ifup` | `micctrl/network.c:3939` | 启用主机网卡 | 路径通常仍在，但更稳的是换 `ip link set up` |
| `/sbin/ifdown` | `micctrl/network.c:3970` | 关闭 | 同上 |
| `/etc/rc.d/network` | `micctrl/network.c:3908` | SUSE 分支重启网络 | 改成 `systemctl restart` 对应服务 |
| `/bin/gzip` | `libmpssconfig/genfs.c:1683,1688,1700,1706`、`verify_bzimage.c:178-186` | 解压卡内核镜像 | 命令本身与架构无关，可留 |
| `/bin/cpio` | `libmpssconfig/genfs.c:1719-1737` | 从卡 initramfs 取写文件 | 命令与架构无关，可留 |
| `/usr/bin/ssh-keygen` | `micctrl/user.c:2872` | 给卡生成 SSH 主机密钥 | 与架构无关，可留 |
| `systemctl` / `service` | `mpss-micmgmt/apps/mpssdebug/micdebug.sh:607,611` | 调试脚本抓服务状态 | 二者互备，留一个 |
| `lspci` / `pciutils` / `modprobe` / `setpci` | **零命中** | 已审计 `mpss-daemon` 全部 31 个源文件与 `mpss-micmgmt` 源码 | 无需处理 |

　　最后一行还有一个细节：很多移植评估会默认「管理工具肯定会 `lspci` 或 `modprobe`」而预留工作量，这份代码**没有**。`micctrl` 的信息来源是 sysfs 和 `/etc/mpss/micN.conf`，不是命令输出。

---

## 4.6　Python 2.7 是一座孤岛

　　用户态里有一批 **Python 2.7** 代码：39 个 `.py`、378,128 字节。它们不是外围脚本，而是几个正式组件：

| 组件 | 语言 | 规模 | 状态 |
|---|---|---|---|
| `mpss-micmgmt-python` | Python 2 + `libmicmgmt` 绑定 | 2,198 行 | 需改 Py3 或放弃 |
| `mpss-miccheck` | Python 2 + `ctypes` | 含在 8,603 行里 | 需改 Py3 或放弃 |
| `micperf` 的辅助脚本 | Python 2 | 19 个 `.py` | 同上 |
| `mpss-sysmgmt-micpython` | Python 2.7 脚本 + C 扩展 | 20,319 B + 314,776 B | **C 扩展无源码** → 不可移植 |
| `mpss-miccheck-bin` | **冻结的 Python 2.6 可执行文件**，4.73 MB | 二进制 | 直接弃用 |

　　龙芯新世界上的发行版不会有 Python 2.7。这批代码只有两个去处：改写成 Python 3（`miccheck` 是自检工具，改写有价值），或者连同它的组件一起放弃（`miccheck-bin`、`micpython` 属这一类）。

　　另外要提一句 SDK：`mpss-sdk-k1om` 里有 259 个 Python 2.7 标准库文件，那是 Intel 打包的工具链附属品，不是 MPSS 自己的代码，也不参与主机侧功能，本次不动。

---

## 4.7　没有源码的部分：只能放弃的那一批

　　交付物里有相当一批组件**只给二进制、不给源码**。它们不属于「难移植」，而属于「无法移植」，必须在计划里明确列为放弃项，否则会被误当成负债：

| 组件 | 形式 | 为什么放弃 |
|---|---|---|
| `mpss-micsmc-gui` | 13.5 MB C++ GUI，依赖 `libSDL-1.2` | 无源码，且依赖 SDL 1.2 |
| `mpss-psm` / `mpss-psm-dev` | PSM（InfiniBand 上的 MPI 匹配层） | 无源码，且依赖 Mellanox 驱动 |
| `mpss-mpm` | 远程调试服务 | 无源码（仅 shell 包装） |
| `mpss-sysmgmt-micdiagnostic` | `MicDiag` 硬件诊断 + 4 个 k1om 负载 | 无源码，且依赖 `libSettings`/`libODMDebug` |
| `mpss-sysmgmt-micras` | `micrasd` RAS 守护，428 KB | 无源码 |
| `mpss-sysmgmt-relmon` | `relmond` 可靠性监视，407 KB | 无源码 |
| `glibc2.12pkg-libmicaccesssdk0` | `libMicAccessSDK.so` | 无源码 |
| `glibc2.12pkg-libodmdebug0` | `libODMDebug.so` | 无源码 |
| `glibc2.12pkg-libsettings0` | `libSettings.so` | 无源码 |
| `mpss-miccheck-bin` | 冻结 Python 2.6 | 无源码，且 Py2 已死 |
| `ofed-ibpd` | OFED 的 ibpd | 无源码 |
| `dapl` / `libibscif` / OFED | DAPL over SCIF、IB verbs provider | 有源码，但 LoongArch 无 Mellanox 驱动 → 实际不可行 |
| `mpss-ganglia-web` | Ganglia 卡指标采集 | 代码在**卡侧**，与主机架构无关 |
| `mpss-sdk-k1om` | 659 MB k1om 工具链与 sysroot | 对象是卡，不是主机 |
| `mpss-eclipse-cdt-mpm` | Eclipse 插件 | 与主机架构无关 |
| `meta-mpss-3.8.6.tar.bz2` | 应为 Poky 的 MPSS layer | **归档是 0 字节**，交付缺陷 |

　　每一件「少了什么、用什么顶」的对应关系见[附录 E](E-porting-patches_CN.md) §E.5。这里先记结论：　　这份清单有一个正面意义：**主机侧主链路（`mpssd` / `micctrl` / `libscif` / `libmpssconfig`）上没有任何一个不可替代的第三方 x86 私有库**。ELF 动态段里 96 个 SONAME 逐个查过，挡路的全都在放弃项里。

---

## 4.8　源码级复查：工具端实际要动的地方

　　这一节把 §4.3 的判断落到具体文件上：把 MPSS 3.8.6 交付里的主机侧源码包解出来，逐个扫了一遍。结果比预想更干净。

| 组件 | 源码行数 | 语言 | 架构耦合 |
|---|---|---|---|
| `libscif` | 1,826 | C | 只有 `.symver`（ELF 符号版本指令，与指令集无关） |
| `libmpssconfig` | 约 6,000 | C | 无 |
| `micctrl` | 约 13,000 | C | 无 |
| `mpssd`（主机侧） | 约 1,500 | C | 无 |
| `mpssd`（卡侧，`mpss-micdaemon`） | 1,777 | C | 跑在卡上，与主机架构无关 |
| `libmicmgmt` 与 `mpssinfo`／`mpssflash` | 约 20,000 | C++ | 无 SIMD、无 `cpuid` |
| `miccheck` | 1,330 | Python | 依赖 `libmicmgmt.so.0` |
| `mpss-coi` | 57,578 | C++ | `cpuid` 31 处、`rdtsc` 1 处 |
| `mpss-myo` | 29,741 | C++ | SSE2 内在函数（`consistent/myodiff.c`）、`__x86_64__` 2 处 |

　　扫描口径是在这六棵源码树里搜 `__x86_64__`、`__i386__`、`cpuid`、`_mm_`、`MSR`、`iopl`、`/dev/cpu`。**核心四件（`micctrl`、`mpssd`、`libmpssconfig`、`libscif`）里一处都没有**；全部命中集中在两处 —— COI 的 `src/include/internal/_SysInfo.h`（用 `cpuid` 取 APIC ID 与拓扑）与 `src/api/perf/perf_common.cpp`（用 `rdtsc` 计时），再加上 MYO 的一致性校验文件。这正是 §4.3 那 10 处条件编译与 2 个 SIMD 文件在源码包里的具体位置：COI 与 MYO 占掉了绝大部分，而它们只影响卡上的 offload 编程接口，不影响能不能管这张卡。

　　还有一处结构性证据支持「只是重编」这个判断：`libscif` 的 Makefile 本来就带跨编译判定。

```makefile
TARGET_ARCH := $(shell $(CC) -dumpmachine | sed -n 's/.*\b\([lk]1om\)\b.*/\1/p')
ALL_CFLAGS_k1om = -D_MIC_SCIF_
```

　　`-D_MIC_SCIF_` 只在目标三元组含 `k1om` 或 `l1om`（即卡侧）时才生效。龙芯上 `gcc -dumpmachine` 是 `loongarch64-…`，自动落到主机分支 —— 与内核模块里 `HOST` 的切法是同一套设计。

　　顺带查清了一条一直没细看的接口。micctrl 给卡上建用户、注入密钥走的是 SCIF 管理通道：`libmpssconfig/libmpsscommon.h:43` 到 `:78` 是完整 opcode 表（`MICCTRL_ADDUSER` 为 8、`AU_FILE` 为 11、`AU_DONE` 为 12、`AU_ACK` 为 13、`CHANGEPW` 为 28、`SYSLOG_FILE` 为 31），端口号 `MPSSD_MICCTRL` 为 164（`libscif/scif.h:178`），卡侧接收端就是随卡镜像启动的 `mpssd`（`mpss-micdaemon-3.8.6/mpssd.c`）。密钥那一步有两层做法：卡从主机上的文件系统目录启动时，micctrl 直接改那份目录里的 `etc/passwd`／`etc/shadow`／`home/<user>/.ssh/authorized_keys`；卡从 RAM 根启动且已 booted 时，它把这些内容按上面的 opcode 通过 SCIF 发给卡上的 `mpssd`，由卡侧落盘。**卡端的服务端已经在卡上运行，主机端缺的只是 `libscif` 加一个发这几个 opcode 的客户端** —— 这是把管理面拿回来的最短路径。

### 真机结果：这张表已经全部编成并跑起来了

　　上面那份「预计要动多少」的判断，在龙芯上落实成了一轮完整的构建与运行验证。结论是：**表格里列出的每一个组件都编出来了，其中四个当场跑通**。逐条证据与全部改动清单收在[附录 E](E-porting-patches_CN.md)，这里只留结论。

| 组件 | 结果 |
|---|---|
| `libscif`、`libmpssconfig`、`mpssd`、`micctrl` | 全部编成；`micctrl --status` 报出 `mic0: online`，`micctrl --initdefaults` 生成配置与卡镜像目录 |
| `libmicmgmt`、`mpssinfo`、`mpssflash` | 全部编成；`mpssinfo` 读出卡的 `Vendor 0x8086`／`Device 0x225c`／`Family 0x0b`／`Stepping C0`／`SKU C0PRQ-7120 P/A/X/D` |
| `miccheck` | 移植到 Python 3 后跑通，主机侧四条默认测试里前三条通过 |
| `libcoi_host.so` | 编成；外部程序按公开 ABI 名链接通过 |
| `libmyo-client.so.0` | 编成、链接并运行，调用进入了库内代码路径 |

　　值得一提的是改动量的分布：**核心四件（约 2.4 万行 C）合起来只动了三处源码** —— 一处发行版探测、一处原代码自带的未定义行为、一处指针与 0 比较。剩下的改动几乎全在「工具链变严」这一类上（新 binutils 对 `.symver` 的限制、GCC 14 把隐式函数声明与指针类型不兼容升为错误、GCC 10 的 `-fno-common` 默认值），而它们与指令集无关 —— 拿今天的 x86-64 工具chain 编这份 2016 年的代码，同样要面对它们。这反过来印证了 §4.3 的判断：**这一层没有移植问题，只有重编问题**。

　　移植顺序上的结论：上表头四行（核心管理面，约 2.4 万行 C）按「重编加修编译期摩擦」推进即可，验收锚点也是现成的（`micctrl --status` 走 sysfs，`--sshkeys` 走 SCIF 且卡端 `mpssd` 会打 `[UserAdd] … Success`）；COI 与 MYO 排在后面，按需再做。

## 4.9　本章结论

　　三句话。

　　第一，主机用户态**不需要移植**，只需要重新编译。整棵树里与 x86 直接相关的只有 10 处条件编译和 2 个用 SIMD 内在函数的文件；没有硬编码的 x86 路径，没有 SSE/AVX 扩散，没有内联汇编。

　　第二，真正需要动手的是**外部命令**和 **Python 2.7** 这两块：`brctl`/`ifconfig` 在新内核上已经不推荐或废弃，要用 `ip` 系列重写；那 39 个 Python 2.7 脚本要么改写成 Py3，要么连同组件放弃。

　　第三，交付里有一批组件没有源码，它们不是「以后再说」，而是**一开始就该划掉**。把放弃项先划掉，剩下的工作量才看得清：主机用户态的实际改造面，是 `micctrl`/`libmpssconfig` 那约 2.7 万行 C 里的网络与路径部分，加上几个测试/自检工具。

　　这一层的风险等级与内核模块完全不同：它的失败模式是**路径找不到、命令报错**，是能看见、能改的；而第五章登记的那几条 F 级风险是静默的。所以从工程顺序上说，用户态这一层应该排在后面做 —— 先把卡能点亮，再管这些工具。理由见 [第八章　移植路线](08-migration-roadmap_CN.md)。
