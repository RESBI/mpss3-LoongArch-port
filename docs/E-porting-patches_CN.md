# 附录 E　工具端移植补丁清单

　　这一份是本报告正文之外的施工记录：把 MPSS 3.8.6 的主机侧工具端搬到龙芯上，实际动过的每一处、以及每一处的判据。全部改动都有脚本可重跑，脚本与日志留在服务器上的 `~/XeonPhiX100-LoongArch/`（见第八、十二章的说明）。

## E.1　构建结果总表

　　环境是 AOSC OS 13.3.1、内核 7.1.13-aosc-main-16k、gcc 15.3.0、glibc 2.42、python 3.14；卡是 7120P，驱动是本报告第八章那棵树编出来的 `mic.ko`。

| 组件 | 结果 | 可核对的证据 |
|---|---|---|
| `libscif` | 编出 `libscif.so` | LoongArch ELF、SONAME `libscif.so.0`、导出 22 个符号且带 `@@SCIF_0.0`、只依赖 libc |
| `libmpssconfig` | 编出 `libmpssconfig.so` | LoongArch 共享库，422 KB |
| `mpssd`（主机守护进程） | 编出 | LoongArch PIE 可执行 |
| `micctrl` | 编出且跑通 | `micctrl --status` 输出 `mic0: online (mode: linux image: …/bzImage-knightscorner)` |
| `libmicmgmt` | 编出 | LoongArch 共享库，依赖 libscif 与 libstdc++，SONAME `libmicmgmt.so.0` |
| `mpssinfo` | 编出且跑通 | 读出 `0x8086`／`0x225c`／`0x7d95`、`Family 0x0b`、`Stepping C0`、`SKU C0PRQ-7120 P/A/X/D` |
| `mpssflash` | 编出 | 用法输出正常（update／version／read／check／device） |
| `micsmc`（命令行版） | 编出且跑通 | 打印 `Intel(R) Xeon Phi(TM) Coprocessor Platform Control Panel VERSION: 3.8.6-1` |
| `miccheck` | Py3 移植后跑通 | **8 项全部通过，`Status: OK`**（主机 4 项 + 设备 4 项） |
| `libcoi_host.so` | 编出且 ABI 名可用 | 11.6 MB LoongArch ELF；外部程序调用 `COIEngineGetCount` 链接通过 |
| `libmyo-client.so.0` | 编出、链接并运行 | 外部程序调用 `myoArenaCreate` 成功进入库内代码路径（库自己打出参数校验错误） |

## E.2　逐条改动

　　表的读法：「位置」是原始交付里的位置，「处置」是这一版实际做的事。凡是「原代码缺陷」一类的，说明它在 x86 上本来就不成立，只是当年的编译器没计较。

| # | 包 | 位置 | 现象 | 处置 |
|---|---|---|---|---|
| 1 | mpss-daemon | `libmpssconfig/micenv.c:122` | 环境初始化报 `Distribution from system probe unknown`（只探测 Red Hat／SUSE／Ubuntu 的网络目录） | 补 AOSC／LoongArch 探测分支。**不用 `MPSS_DIST` 环境变量绕**：`micenv.c:112` 那条分支会把 `live_update` 置假，而给运行中的卡推用户与密钥的 SCIF 通路依赖它为真 |
| 2 | mpss-daemon | `libmpssconfig/passwd.c:102`、`:108`、`:114`、`:120`、`:126` | `micctrl --initdefaults` 段错误，栈是 `parse_shadow` ← `add_shadow` ← `create_base_users` | `*lastd[1]` 这类写法实为 `*(lastd[1])`，读的是相邻栈槽里的未初始化指针再解引用；按原意改为 `(*lastd)[1]` |
| 3 | mpss-daemon | `libmpssconfig/genfs.c:1752` | C++ 下 `if (getcwd(cwd, PATH_MAX) < 0)` 非法（指针与 0 比大小） | 改为 `== NULL` |
| 4 | 构建工具 | `gen-symver-map`（同版本源包） | Python 2 脚本（`ConfigParser`、`print >>` ），现代发行版没有 python2 | 移植到 Python 3；`libscif` 的版本脚本就由它生成 |
| 5 | mpss-micmgmt | `apps/mpssinfo/helper.h:44`–`:46`、`apps/mpssflash/helper.h:109`–`:110` | `inline function declared but never defined`（GCC 14 起是错误） | 去掉 `inline`；同文件的 Windows 分支本来就是普通声明 |
| 6 | miccheck | 全树 9 处 | `except X, e:` 是 Python 2 语法 | 改 `except X as e:` |
| 7 | miccheck | `bin/miccheck.py`、`mk/get_versions.py` | shebang 是 `python`，AOSC 只有 `python3` | 改 `python3` |
| 8 | miccheck | `_miccheck/linux/tests.py:80` | `TypeError: a bytes-like object is required, not 'str'` | `subprocess.communicate()` 在 Py3 返回 bytes，统一解码 |
| 9 | miccheck | `_miccheck/linux/tests.py:55` | 外部命令写死 `/sbin/…`（Red Hat 风格） | 绝对路径不存在时退回按名字查 PATH |
| 10 | miccheck | `_miccheck/common/main.py:187` | 兜底 `except` 只打一行消息，调试看不到栈 | 加 `traceback.print_exc()` |
| 11 | mpss-coi | `Makefile:184`–`:188` | `WHAT := $(WHAT_$(TARGET_MACHINE))` 表里只有 `k1om`／`x86_64`／`poky`，龙芯落空，按 `$(WHAT)` 分派的 `all` 规则全部不成立 | 加 `WHAT_loongarch64 := HOST` |
| 12 | mpss-coi | `src/include/internal/coi_version_asm.h` | 汇编器报 `invalid attempt to declare external version name as default`（83 条 `.symver`，被 13 个源文件包含而每个只定义其中几个） | 非 x86 目标不发射这段汇编 |
| 13 | mpss-coi | `src/include/internal/_SysInfo.h:143`、`:167` | `error: output number 1 not directly addressable`（`cpuid` 内联汇编） | 非 x86 把输出清零；`GetAPICID()` 改用 `sched_getcpu()` |
| 14 | mpss-coi | `src/api/perf/perf_common.cpp:53`、`:68` | `rdtsc` 内联汇编 | 改 `CLOCK_MONOTONIC` 纳秒计数，并把频率固定为 1e9，使 `counter/frequency` 仍是秒 |
| 15 | mpss-coi | `src/include/internal/_DMA.h:110` | `static const double` 类内初始化在 C++11 起需要 `constexpr` | 加 `#if __cplusplus` 分支 |
| 16 | mpss-coi | 链接期 | 关掉 `.symver` 之后公开 ABI 名（`COIEngineGetHostname@@COI_1.0`）消失，库内跨文件调用也断裂 | 由版本汇编头生成 `-Wl,--defsym,公开名=实现名`（63 条），配合既有版本脚本 |
| 17 | mpss-myo | `src/include/myobasictypes.h:53` | `INTEL64` 只从 `_WIN64`／`__x86_64__` 推导，龙芯落空，于是 `myoconfig.h:48` 把 `MYOI_VSM_START_ADDR` 定义成常量宏，而 `myoosplatform.c:658` 要给它赋值 | 把龙芯并入 64 位推导（头文件自己的注释写的就是「64 位 Unix-like 系统」） |
| 18 | mpss-myo | `src/allocator/myomemoryallocator.c:140` | `popcount32`／`nlz32` 只定义在 `#ifdef WINDOWS` 里，Linux 上靠隐式函数声明通过（GCC 14 起是错误） | 改成无条件定义 |
| 19 | mpss-myo | `src/machinedep/myoosplatform.c:432` | `struct ucontext` 与 `REG_ERR` 是 x86 信号上下文 | 非 x86 无法区分读／写缺页，保守按「写」处理 |
| 20 | mpss-myo | `src/include/myotime.h:53` | `rdtsc` 内联汇编 | 改 `CLOCK_MONOTONIC` 纳秒（tick 只用于做差，量纲自洽） |
| 21 | mpss-myo | `src/misc/myoatomic.c:51` | `lock; xaddl` | 改 `__atomic_add_fetch(loc, value, __ATOMIC_SEQ_CST)` |
| 22 | mpss-myo | `src/machinedep/myoosplatform.c:435` | 把 `sa_sigaction` 赋给 `void (*)()`，GCC 14 起指针类型不兼容是错误 | 按真实类型声明并补调用参数 |
| 23 | mpss-myo | `src/include/myo_version_asm.h` | 84 条 `.symver`，同第 12 条 | 加架构守卫 |
| 24 | mpss-myo | `src/include/MYOMacros_common.h:42` | 实现名带 `1` 后缀、公开名靠 `.symver` 别名 | 龙芯并入「不带版本后缀」分支（该分支原本就是卡侧与 Windows 走的）。**MYO 没有多版本符号，所以这么做安全**；COI 有两个（`COIProcessLoadLibraryFromFile`／`FromMemory`），因此 COI 走的是第 16 条的别名方案 |
| 25 | mpss-myo | `src/linker_script.map` | 版本脚本的 `local: *` 把实现名一并隐藏，别名随之消失（库里只剩 6 个动态符号，公开名完全不见） | `global` 段补上实现名 |
| 26 | mpss-daemon | `micctrl/user.c:2888` | `--initdefaults` 报 `Create failed for /etc/ssh/ rsa1 keys: Unknown error 255`（同一处 dsa 也报） | 生成卡端主机密钥的密钥表用了 `rsa1` 与 `dsa`，现代 `ssh-keygen` 已不支持；改成 `ed25519`／`rsa`／`ecdsa`，`init.c` 的持久化文件表同步 |
| 27 | miccheck | `_miccheck/common/micdevice.py:81` | 设备自检报 `SMC firmware version does not match, should be '', it is 'b'1.17.6900''` | `ctypes.create_string_buffer().value` 在 Python 3 返回 bytes，`str()` 后带上 `b'...'`；加一次 decode |
| 28 | mpss-daemon | `micctrl/network.c:873` | `--hostkeys=<目录>` 永远报 `Warning - only found 4 of normal 8 files`，且目录里的 ed25519 密钥被静默忽略 | `--hostkeys` 判断「是不是正经密钥文件」用的 `hostkeynames[]` 表还是 `dsa` 与协议 1 的 `ssh_host_key`，而补丁 19 已把生成端改成 `ed25519`／`rsa`／`ecdsa` —— 两端不一致。把该表同步为 6 个现代名字（`LEN_KEYNAMES` 由 `sizeof` 自动变 6） |

## E.3　不改代码、只用构建开关的

| 项 | 原因 | 开关 |
|---|---|---|
| MYO 的 SSE2 差分层 | `src/consistent/myodiff.c` 自带按元素宽度选择的标量回退 | `-DMYOI_DIFF_I64` |
| MYO 的暂定定义重复 | GCC 10 起默认 `-fno-common`，`myoimpl.h:556`–`:557` 的 `myoiMyId`／`myoiInitFlag` 在多个目标文件里各定义一份 | `-fcommon` |
| MYO 缺 C++ 运行时 | 库用 `gcc` 链接含 C++ 目标文件 | 链接加 `-lstdc++` |
| MYO 手册生成 | 需要 `a2x`（asciidoc），不装也能用 | `BUILD_AND_INSTALL_MAN_PAGES=0` |
| micmgmt 的 `-Werror` | app 的 Makefile 把 `-Werror` 放在 `CFLAGS` 之后，覆盖不掉 | 直接覆盖 `EXTRA_CFLAGS` |

## E.4　口径与边界

　　需要说清楚三件事。

　　第一，这里改的是**构建与运行所必需**的部分，没有为了「更现代」而重构。凡是能用构建开关解决的（E.3），一律不动源码。

　　第二，`.symver` 这一类问题的本质是**工具链变严**而不是架构变化：新 binutils 拒绝给「本目标文件里未定义」的符号声明默认版本。它在 2016 年的 k1om 工具链上能过，在今天的 LoongArch 工具链上不能过 —— 换成今天的 x86-64 工具链同样过不去。这属于第六章那类「接口漂移」，只是发生在用户态。

　　第三，卡侧的那份 `mpssd`（`mpss-micdaemon`，1,777 行）**不在移植范围内**：它跑在卡上，用的是随卡镜像发布的 k1om 二进制，与主机架构无关。主机侧要移植的是它的对端。

## E.5　无源码组件的例外与替代方案

　　交付里有一批组件只有二进制、没有源码。它们不是「以后再补」，而是这一版明确划掉的例外；划掉之后要说清楚**少了什么、用什么顶**，否则会被误当成负债。

| 组件 | 为什么进不了这一版 | 在龙芯上用什么顶 |
|---|---|---|
| `libMicAccessSDK` | 全份源码里对 `micaccess` 零命中，是纯预编译件；它服务于 GUI 的设备访问 | `libmicmgmt`（有源码，本版已编成）覆盖同类设备查询：`mpssinfo` 读出的 `SKU`／`Family`／`Stepping` 就是它给的 |
| `libSettings`、`libODMDebug` | 无源码，被 GUI 与 MicDiag 使用 | 本版工具链不依赖它们；诊断改由 `miccheck` 与 `mpssinfo` 承担 |
| `mpss-micsmc-gui` | 无源码的 Qt GUI；`apps/micsmc` 里那份是**命令行版**（6,105 行，只依赖 libscif 与 libmicmgmt，本版已编成） | 命令行 `micsmc` 加 `mpssinfo`；要图形界面就得另写，AOSC 上有 Qt5 可用 |
| `mpss-sysmgmt-micras`（`micrasd`） | 用户态守护无源码。但**内核侧 RAS 有源码**：`ras/micras_*.c` 在模块树里，`micras_api.h`／`micras.h` 也在，`libmicmgmt` 的 RAS 接口本版已编出 | 直接读 `/sys/class/micras/*`，或经 `libmicmgmt`／`miccheck` 取温度、ECC、功耗 |
| `mpss-sysmgmt-relmon`（`relmond`） | 无源码的可靠性监视守护 | 无替代，也不需要：它做的是长期统计告警，与「把卡用起来」无关 |
| `mpss-psm`、`mpss-psm-dev` | 无源码，且是 InfiniBand 上的 MPI 匹配层，依赖 Mellanox 驱动 | 不适用：本机没有 IB 组网，LoongArch 上也没有对应的 Mellanox 驱动 |
| `mpss-mpm` | 无源码（仅 shell 包装）的远程调试服务 | 不需要：调试走 `miccheck`、`mpssinfo` 与卡的串口控制台 |
| `mpss-sysmgmt-micdiagnostic`（`MicDiag`） | 无源码，且带 4 个 k1om 负载 | `miccheck`（本版已移植到 Python 3 并跑通）加 `mpssflash` |
| `mpss-miccheck-bin` | 冻结的 Python 2.6 可执行件 | 不必用：`miccheck` 的 Python 源码在交付里，本版已把它移植到 Python 3 |

　　这张表有个正面读法：**主机侧主链路上没有任何一个不可替代的第三方 x86 私有库**。本版真正编出来的十个组件（附录 E.1）没有一个依赖上表里的东西 —— 依赖只落在 `libscif`、`libmicmgmt` 这些有源码的库上。

## E.6　运行时验证：不只是「编过了」

　　编出来还不算数。这一节是把公开 API 在真机上真调一遍的结果，程序与日志都留在服务器上。

### 库级：libmicmgmt 的 14 项调用

　　写了一个小程序（`verify_miclib.c`）调用 14 个公开 API，逐项与同一时刻的 sysfs 原始值对照。

| 结果 | 项目 |
|---|---|
| **通过 7 项** | 设备枚举（1 个设备）、设备编号、`mic_open_device` 返回 `mic0`、`SKU` = `C0PRQ-7120 P/A/X/D`、`POST 码` = `FF`、`Family` = `0x0b`、`Model` = `0x01` |
| **失败 7 项** | `mic_get_serial_number`、`mic_get_uuid`、`mic_get_memory_info`、`mic_get_cores_info`、`mic_get_version_info`、`mic_get_thermal_info`、`mic_is_ras_avail` |

　　失败原因查清了，**不是移植缺口**：这七个全部走 `scif_request()`（`knc_device.cpp` 里共有 34 处这样的调用），也就是要和卡上的 `mpssd` 通过 SCIF 对话；而 `/dev/mic/scif` 的权限是 `crw------- root root`，普通用户打不开。对照证据是：所有 sysfs 支撑的调用（`sku`／`post_code`／`family`／`model`／设备枚举）**全部通过**，而 sysfs 里确实有这些属性（`sku`、`postcode`、`family`、`model`、`stepping`、`memsize`、`flashversion`、`serialnumber` 等，权限 `r--r--r--`）。

　　这条界线值得记下来：**这台机器上，MPSS 工具的「读 sysfs」部分不需要特权，「走 SCIF」部分需要**。MPSS 自己的做法是把 `micctrl` 装成 setuid root（`micctrl/Makefile:37` 的 `INSTALL_s = $(INSTALL) -m 4755`），正是为了这个。

### 库级：MYO 的两个关键调用

　　移植 MYO 时把 x86 的 `rdtsc` 换成了 `CLOCK_MONOTONIC` 纳秒（补丁 20），这件事必须量一下才算数：

| 调用 | 实测 |
|---|---|
| `myoMyId()` / `myoNumNodes()` | `0` / `1`（单节点） |
| `myoTicks()` 在 250 毫秒睡眠前后的差 | **250,101,315** —— 单调递增，且正好是纳秒量纲 |
| `myoWallTime()` 同一段时间的差 | 250,100 微秒 |

　　这条差值就是「rdtsc 换成单调时钟」移植生效的直接证据：数值单位对、单调性对、与真实时长吻合。

### 库级：COI 的错误路径

　　`COIEngineGetCount()` 与 `COIEngineGetHandle()` 在卡侧 `coi_daemon` 未运行时分别返回 1 与 5，句柄为空 —— **优雅返回错误码而不是崩溃**。这验证的是错误路径：`libcoi_host.so` 的 SCIF 连接失败被正确转成了 COI 的错误码。

### 边界

　　这几项验证的共同边界是：**依赖卡侧服务的功能（SCIF 管理通道、COI 的 daemon、MYO 的 service）没有在「未安装到系统路径、未以 root 运行」的状态下验证**。它们要等系统安装那一步（`g9_00_install_root.sh`）跑完，那里会以 root 重跑同一套探针并补上这些格。

## E.7　真机验收记录

　　这一节记的是「按 E.1 装好之后，逐条命令在龙芯上跑出来什么」。它不是计划，是已经发生的事实；每条都注明了证据出处，便于复核。

### E.7.1　逐项结果

| 项目 | 命令 | 实测结果 |
|---|---|---|
| 设备识别 | `micctrl --status` | `mic0: online (mode: linux image: /usr/share/mpss/boot/bzImage-knightscorner)` |
| 配置与卡镜像生成 | `micctrl --initdefaults` | 生成 `/etc/mpss/mic0.conf` 与卡镜像目录 `/var/mpss/mic0`，其中已有 `etc/passwd`（root／sshd／nobody／nfsnobody／micuser／resbi，后两者由主机 passwd 合并而来）、`etc/network/interfaces`（`auto mic0` 加 `address 171.31.1.2`）、`etc/ssh` 下的 ed25519／ecdsa／rsa 主机密钥、`home/<user>/.ssh/authorized_keys` |
| 库功能验证 | `verify_miclib`（14 个公开 API） | 普通用户下 7 项通过、7 项失败；**以 root 重跑 18 项全部通过** |
| 读出的卡参数 | 同上 | SKU `C0PRQ-7120 P/A/X/D`、序列号 `ADKC50600016`、UUID `735f57a0-…`、61 核、显存 16,252,928 KB（约 15.5 GiB）、显存厂商 Samsung、核心温度 48 摄氏度、PCIe x1 @ 5 GT/s、Flash `2.1.02.0391`、卡上 OS `2.6.38.8+mpss3.8.6`、POST 码 `FF`、RAS 可用 |
| 自检 | `miccheck` | **8 项全部通过，`Status: OK`**：主机侧 4 项（设备数、驱动已载、驱动看到的设备数、mpssd 在跑）与设备侧 4 项（online 且 postcode=FF、RAS daemon 可用、flash 版本 `391`、SMC 固件版本 `1.17.6900`） |
| 守护进程 | `systemctl start mpss` | `active`；单元用 `Type=simple` + `mpssd -l`；日志里 mpssd 自己拼出卡的内核命令行 `quiet root=ramfs console=hvc0 cgroup_disable=memory highres=off noautogroup micpm=cpufreq_on;corec6_on;pc3_on;pc6_on` |
| 主机↔卡握手 | 重启卡端 mpssd | 主机日志 `mic0: Monitor connection established`；卡端日志 `[Start] Connected to host mpssd success`；卡端 mpssd 线程数 1 → 3（说明端口 164 的监听线程已创建） |
| offload 库运行时 | `probe_runtime` | MYO 的 `myoTicks()` 在 250 毫秒睡眠前后差 **250,101,315**（纳秒量纲与单调性都对），`myoWallTime()` 差 250,100 微秒；COI 在卡侧 daemon 缺席时返回错误码 5 而非崩溃 |
| **SCIF 加用户闭环** | `micctrl --useradd=resbi` | 主机侧 MicDir 写入 `resbi` 与 `.ssh/authorized_keys`；**卡端 `/var/log/mpssd` 出现 `[UserAdd] 'resbi' Success`**；卡端 `/etc/passwd` 出现 `resbi:x:1000:1001:Resbi:/home/resbi:/bin/bash`，`/home/resbi/.ssh/` 下 `authorized_keys`（861 字节）与各 `.pub` 落地；随后以该身份登录成功：`uid=1000(resbi) gid=1001 groups=1001`，主机名 `knightscorner` |

　　其中最值得记的是「普通用户 7 通过、root 18 通过」这一组对照：它把 MPSS 工具在这台机器上的**特权边界**画了出来（见 E.7.2）。

### E.7.2　两条行为约束（文档没写，实测才知道）

　　**第一条：SCIF 与 sysfs 的特权边界不同。** `verify_miclib` 里失败的 7 项与通过的 7 项，分界线正好是「读 sysfs」还是「走 SCIF」：设备枚举、SKU、POST 码、Family、Model 这些读 `/sys/class/mic/mic0/*`（权限 `r--r--r--`），普通用户就能拿到；而序列号、UUID、显存信息、核心信息、版本信息、温度、RAS 这些走 `scif_request()`（`knc_device.cpp` 里共 34 处），要先打开 `/dev/mic/scif`，而那是个 `crw------- root root` 的设备。这也解释了 MPSS 为什么把 `micctrl` 装成 setuid root（`micctrl/Makefile:37` 的 `INSTALL_s = $(INSTALL) -m 4755`）。

　　**第二条：卡端 mpssd 的 MONITOR_START 握手有时序要求。** 卡端 `mpssd` 启动时的顺序是——

```text
1. bind 端口 163（MPSSD_MONSEND）并 listen
2. scif_connect({node 0, port 160 = MPSSD_MONRECV}) 连主机 mpssd，发 MONITOR_START
3. scif_accept 等主机回连 163
4. 握手成功之后，才 pthread_create(micctrl_mon) —— 端口 164（MPSSD_MICCTRL）到这里才绑定
```

　　也就是说：**主机 mpssd 必须在卡端 mpssd 启动的那一刻正在监听 160 端口**，否则第 2 步那一次 MONITOR_START 没人接，端口 164 永远不会出现，之后所有走 `MPSSD_MICCTRL` 的操作（例如 `micctrl --useradd`）都会连到一个不存在的监听者。我们第一轮正是手工用 sysfs 引导的卡、当时主机 mpssd 没运行，于是撞上了这个坑；把卡端 mpssd 在主机 mpssd 运行时重启一次，握手立刻补上（E.7.1 最后两行就是证据）。

### E.7.3　一处原代码缺陷：失败的连接会进内核无限重试

　　`micscif/micscif_api.c:728` 起的等待循环值得单独记一笔：

```c
while ((err = wait_event_interruptible_timeout(ep->conwq,
        (ep->state != SCIFEP_CONNECTING), NODE_ALIVE_TIMEOUT)) <= 0) {
    ...
retry:
    err = wait_event_timeout(ep->diswq, (ep->state != SCIFEP_CONNECTING), NODE_ALIVE_TIMEOUT);
    if (!err && scifdev_alive(ep))
        goto retry;          /* 对端不应答、设备还活着，就无限重试 */
    ...
}
```

　　当连接目标不存在时：等超时 → 发终止消息 → 再等 → 仍无应答 → `goto retry`。整段在内核里循环，**不返回用户态**，于是 `SIGTERM` 与 `SIGKILL` 都不能终止它（`timeout 300` 发出的 SIGTERM 也一样）—— 实测中那个 `micctrl --useradd` 就是这样变成不可杀进程的，最后只能靠重置卡让 `scifdev_alive()` 变假才退出。这是原代码的行为（对端正常时应答时不会走到这条路径），不是移植引入的，但在「对端没起来」这种最常见的调试场景下会咬人，值得记下来。

### E.7.4　闭环：micctrl 在卡上建用户并注入密钥

　　E.7.2 第二条那条时序约束补上之后，最初那个问题 ——「micctrl 怎么给卡上创建用户和注入 key」—— 就在龙芯上走完了全程。一次 `micctrl --useradd=resbi` 做了两件事：

　　一是**离线**：在主机上的卡镜像目录里写入 `etc/passwd` 的 `resbi` 行，并把主机用户 `~/.ssh` 下的 `*.pub` 汇成 `<MicDir>/home/resbi/.ssh/authorized_keys`（这正是 `add_ssh_info()` 与 `create_authfile()` 的行为，见第四章 §4.8）。

　　二是**上线**：经 SCIF 把同样的内容推给正在运行的卡 —— `user.c:1500` 的 `adduser_remote()` 连到 `{node 1, port 164}`，按 `libmpsscommon.h:43` 起的 opcode 表发 `MICCTRL_ADDUSER`、逐文件 `MICCTRL_AU_FILE`、再 `MICCTRL_AU_DONE`。卡端 `mpssd` 收到后落盘并记一行日志：

```text
Sun Oct  4 19:48:42 2026: [UserAdd] 'resbi' Success
```

　　随后 `ssh resbi@171.31.1.2` 直接登录成功（`uid=1000(resbi) gid=1001`）—— 这个用户与它的密钥都是 micctrl 造的、经 SCIF 送上去的，没有手工改过卡端镜像。

　　顺带记一条无害的告警：`[Warning] mic0: Create home directory /var/mpss/mic0/home/resbi failed: File exists` —— 因为 `--initdefaults` 已经建过这个目录，不影响结果。

## E.8　发布版：release

　　把上面这些包整理成了一份可直接分发的发布树 `~/XeonPhiX100-LoongArch/release`（发布时打的包为 `release_v0.1.tar.gz`）。设计目标是「每个包一个目录，各自 `make install` 即可」，不依赖任何集中式安装脚本。

### E.8.1　结构

| 目录 | 内容 | 装到哪里 |
|---|---|---|
| `00-build-tools` | `gen-symver-map`（Py3 版）、`gen-defsym.py`、`mpss-metadata` | 供其它包调用 |
| `02-libscif` | `libscif.so` 与 `scif.h`／`scif_ioctl.h` | `/usr/lib64`、`/usr/include` |
| `03-mpss-daemon` | `libmpssconfig.so`、`mpssd`、`micctrl`（setuid root） | `/usr/lib64`、`/usr/sbin`、`/usr/include/mic` |
| `04-mpss-micmgmt` | `libmicmgmt.so`、`mpssinfo`、`mpssflash`、`micsmc` | `/usr/lib64`、`/usr/bin`、`/usr/include` |
| `05-miccheck` | `miccheck`（Py3 版） | `/usr/bin`、`/usr/src/miccheck` |
| `06-mpss-coi` | `libcoi_host.so` | `/usr/lib64`、`/usr/include/intel-coi` |
| `07-mpss-myo` | `libmyo-client.so` | `/usr/lib64`、`/usr/include` |
| `08-mic-module` | `mic.ko` 及 modprobe／udev 配置、内核头 | `/lib/modules/$(uname -r)`、`/etc`、`/usr/include/mic` |
| `09-boot-images` | 卡端 `bzImage` 与 initramfs | `/usr/share/mpss/boot` |

　　每个包目录里有两个 Makefile：`Makefile.mpss` 是上游原始文件（补丁已打在里面），`Makefile` 是发布版包装（构建 + 按 MPSS 期望的路径安装）。另有顶层 `Makefile` 可一键按序装完，以及两份说明文件 —— README_CN.md（中文）与 README.md（英文，内容对应），并随附 `images/screenshot.png`（实测机器的主机与卡内截图：`lspci` 认出 `Xeon Phi coprocessor SE10/7120 series`，主机为龙芯 3A6000，卡内 `uname -a` 为 `2.6.38.8+mpss3.8.6` k1om、61 核）。两份都含安装顺序、装完怎么配 `mic0.conf`、systemd 单元怎么写、六条已知注意事项，并都写明了**版权与许可**（上游归 Intel，移植与打包部分归移植版贡献者）、**免责声明**（实验性移植、可能损坏硬件或数据、验证范围只到单机基本路径）以及**本次适配由 DeepSeek-V4.1 Flash 辅助完成**的说明。

### E.8.2　自测记录

　　在龙芯上按「干净树 → 逐包 `make install DESTDIR=…` → 核对产物 → 编译链接运行」验过一遍：

| 步骤 | 结果 |
|---|---|
| 逐包安装（干净源码树，`DESTDIR=/tmp/relstage6`，`PREFIX=/usr`） | **9 个包全部成功**，耗时 0 秒（libscif／miccheck／boot-images）到 64 秒（COI）不等 |
| 产物核对（29 项：5 个库及其符号链接、6 个可执行、13 个头文件／模块、2 个引导镜像、3 个配置文件） | **0 项缺失**；`micctrl` 为 `-rwsr-xr-x`（setuid 位正确） |
| 端到端编译链接运行 | 用规范写法 `#include <intel-coi/source/COIEngine_source.h>` 编译，链接 `-lcoi_host -lmyo-client` 通过；运行输出 `COI 引擎数=0`（卡侧 daemon 未运行，属预期）、`MYO 节点数=1`、200 毫秒睡眠的 `myoTicks` 差值 **200,103,750**（纳秒量纲正确） |
| 复位 | `make clean` 后构建残留 0 个，发布树 47 MB（源码与镜像） |

### E.8.3　自测中踩到并修掉的一个坑

　　包装 Makefile 的 `build` 目标最初写成 `$(MAKE) …`（没带 `-f Makefile.mpss`）。因为包装文件本身就叫 `Makefile`，这条命令会再读它自己，于是**无限递归**：自测时它一路递归到 `make[7]`，日志涨到 42 万行、31 MB，机器内存被吃满。修法是所有调用一律显式 `-f Makefile.mpss`（或 `-C 子目录`）。这条已写进 `08-mic-module/Makefile` 的注释里，作为发布版的注意事项留档。

### E.8.4　真实安装暴露的第二个包装层错误：不要把 `/usr/include` 塞进 `C_INCLUDE_PATH`

　　E.8.3 那个递归坑修好之后，逐包 `make install` 在**暂存安装**（`DESTDIR=/tmp/…`）下全部通过，但换成**真实安装**（`sudo make install`，`PREFIX=/usr`）时，`04-mpss-micmgmt` 在编译第一个 C++ 文件就失败：

```text
C_INCLUDE_PATH=/usr/include ... g++ ... -c src/mic_device.cpp
/usr/lib/gcc/loongarch64-aosc-linux-gnu/15/include/c++/cstdlib:83:15:
    fatal error: stdlib.h: No such file or directory
   83 | #include_next <stdlib.h>
```

　　原因是包装 Makefile 无条件地把 `$(DESTDIR)$(PREFIX)/include` 放进 `C_INCLUDE_PATH`；真实安装时它展开为 `/usr/include`，而那是编译器本来就有的系统目录，重复出现一次之后，libstdc++ 的 `<cstdlib>` 里那句 `#include_next <stdlib.h>`（含义是「从当前目录之后继续找」）就跳过了真正的 `stdlib.h`。暂存安装之所以不暴露，是因为 `<stage>/usr/include` 里只有我们自己装的几个头文件，不含 `stdlib.h`。

　　修法：只在「`DESTDIR` 非空」或「`PREFIX` 不是 `/usr`」时才显式给出搜索路径，`03-mpss-daemon`、`04-mpss-micmgmt`、`06-mpss-coi`、`07-mpss-myo` 四个包装 Makefile 均如此处理。修后验过 `make build PREFIX=/usr` 四个包全过，暂存安装也仍然可用。

　　这条也提醒了一件操作上的事：**直接 `sudo make install` 会以 root 身份完成构建**，在源码树里留下 root 属主的目标文件，之后以普通用户重新构建就会报 `can't create objs/xxx.o: Permission denied`。推荐先以普通用户 `make`、再 `sudo make install`；两份 README 都已写明，万一已经这样装过，`sudo make clean` 或 `sudo chown -R $USER .` 即可恢复。

### E.7.5　`micctrl --hostkeys` 的用法与一处两面不一致

　　这条选项目的语义在源码里写得很清楚（`micctrl/network.c:157` 的注释与 `:164` 的帮助文本）：

```text
micctrl [global options] --hostkeys=<dir> [sub options] <Xeon Phi card>
The --hostkeys copies the ssh host keys from the specified directory to the Xeon
Phi file systems /etc/ssh directory. ... There is not coresponding micctrl option
to save the keys.
```

　　三点容易踩：

1. **参数必须用 `=` 连接**。该选项在 `micctrl.c:83` 里声明为 `optional_argument`，写成 `--hostkeys <目录>` 时目录会被当作**设备名**解析，于是报 `Error: Invalid device name`；只有 `--hostkeys=<目录>` 才是把目录当参数。
2. **需要真正的 root**。它检查的是 `getuid()`（不是 `geteuid()`），所以即便 `micctrl` 装了 setuid 位，普通用户跑也会得到 `[Error] Only root is allowed to use this option`。
3. **方向是「主机 → 卡」，没有反向选项**。它把目录里的卡端主机密钥复制进 MicDir 的 `etc/ssh`，用来替换 `--initdefaults` 随机生成的那套（让卡每次重建都呈现同一组主机密钥）；想反过来把卡的密钥收进 `known_hosts`，得自己从 `/var/mpss/mic0/etc/ssh/*.pub` 取。

　　另外这里还有一处**因补丁 19 而暴露的两面不一致**：`--hostkeys` 只认 `hostkeynames[]`（`network.c:873`）里列的文件名，而那张表当时还是 `dsa` 与 `ssh_host_key`（协议 1）那套，与已经改成 `ed25519`／`rsa`／`ecdsa` 的生成端对不上 —— 结果是目录里的 ed25519 密钥被静默忽略，计数也永远到不了 `LEN_KEYNAMES`，每次都打 `Warning - only found 4 of normal 8 files`。补丁 21 把该表同步为 6 个现代名字后，六个文件齐备时才会输出 `Host ssh files copied from <目录>`。

