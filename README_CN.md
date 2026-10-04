# MPSS 3.8.6 主机侧工具端 · LoongArch 移植版 v0.1

　　这份发布版把 Intel MPSS 3.8.6 的**主机侧工具端**移植到龙芯（loongarch64）。每个包一个目录，各自 `make install` 即可；包与包之间只有「先装库、后装用它编的程序」这一条依赖顺序，没有别的手工步骤。

　　实测环境：AOSC OS 13.3.1、内核 `7.1.13-aosc-main-16k`、gcc 15.3.0、glibc 2.42、Python 3.14、systemd 259，卡为 Xeon Phi 7120P（`8086:225c`）。移植过程、逐条改动与真机验收记录见随附报告的第四、六、八章与附录 E。

![龙芯主机识别到 Xeon Phi 卡，并 SSH 登录卡内](images/screenshot.png)

　　上图是实测那台机器：上半屏 `lspci` 认出 `04:00.0 Co-processor: Intel Corporation Xeon Phi coprocessor SE10/7120 series`，主机为龙芯 3A6000（AOSC OS，内核 `7.1.13-aosc-main-16k`，32 GiB 内存）；下半屏是 `ssh root@171.31.1.2` 登录卡内，`uname -a` 显示卡上内核 `2.6.38.8+mpss3.8.6`（k1om 架构），`/proc/cpuinfo` 显示 61 个核心。这张图概括了整条链路：龙芯主机 → PCIe 上的 Xeon Phi → 卡内的 Linux。

---

## 版权与许可

　　**上游部分。** 本发布版包含的 MPSS 3.8.6 源码、卡端引导镜像与文档，Copyright (C) Intel Corporation，版权归 Intel Corporation 所有，按 Intel 随包提供的许可证分发。各包根目录下的 `COPYING`、`COPYING.LIB`、`COPYING.BSD`、`COPYING.LGPL` 等文件即为对应许可证原文，请以那些文件为准。主机内核模块 `mic.ko` 及其源码按 GPL-2.0 分发。

　　**移植与打包部分。** 为使上述代码在 loongarch64 与当代工具链上可用而做的修改、发布版的组织方式（各包的包装 Makefile、`00-build-tools` 下的辅助工具、本说明文件），Copyright (C) 2026 LoongArch 移植版贡献者，版权归 LoongArch 移植版的贡献者所有，采用与所修改文件原有许可证相同的条款发布。

　　**商标。** Intel、Xeon Phi、Many Integrated Core（MIC）、Knights Corner 是 Intel Corporation 的商标或注册商标。本项目与 Intel 无隶属关系，也未获其背书或支持。

## 免责声明

　　本发布版按「现状」提供，不附带任何明示或默示的担保，包括但不限于对适销性、特定用途适用性与不侵权的担保。使用者自行承担使用风险。

　　需要特别说明的几点：

1. **这是实验性移植，不是 Intel 的官方产品。** 上游 MPSS 3.8.6 发布于 2016 年，Intel 早已停止对 Xeon Phi（Knights Corner）系列及 MPSS 的支持。本移植只做了「让它在龙芯与当代内核／工具链上跑起来」所必需的改动，未做安全性加固，也未做完整性审计。
2. **可能损坏硬件或数据。** 本发布版包含主机内核模块与卡端引导镜像，涉及 PCIe 设备复位、DMA 与卡上固件交互。误用（例如在不适配的硬件上加载模块、写入不匹配的引导镜像、对卡进行 flash 操作）可能导致设备或数据损坏。请在非生产环境先行验证并做好备份。
3. **验证范围有限。** 全部实测只在一台机器上完成（见上文环境），覆盖 `micctrl`、`mpssd`、`mpssinfo`、`mpssflash`、`miccheck`、COI、MYO 与内核模块的基本工作路径，**未覆盖**多卡、大规模 MPI 作业、长时间满载、电源管理与热插拔等场景。
4. **版本匹配由使用者负责。** 发布版中的卡端镜像、`miccheck` 内的 flash／SMC 版本号等都是按实测那台卡填写的常量，换卡后需按实际值重新构建。
5. **不承担任何间接损失。** 在适用法律允许的最大范围内，作者与贡献者对因使用本发布版而产生的任何直接、间接、偶然、特殊或后果性损害不承担责任。

　　若不同意上述条款，请不要使用本发布版。

## 关于本次移植适配

　　本次移植适配工作由 **DeepSeek-V4.1 Flash** 辅助完成：包括上游代码在 loongarch64 上的构建诊断与修补、内核接口漂移的比对与改写、卡端 initramfs 的改制、真机验收脚本的编写与执行，以及随附报告中实测数据的整理与撰写。所有改动的判定与验收均以真机运行结果为准（能跑的命令、卡端 `mpssd` 日志、sysfs 读数），逐条记录见随附报告附录 E。

　　发布版的组织方式（各包包装 Makefile、`00-build-tools`、本说明文件）同样在该协助下完成。使用者仍应自行复核关键改动是否满足自身场景的要求。

---

## 一、包清单与安装顺序

| 顺序 | 目录 | 内容 | 装到哪里 | 依赖 |
|---|---|---|---|---|
| 0 | `00-build-tools` | 构建辅助：`gen-symver-map`（Py3 版）、`gen-defsym.py`、`mpss-metadata` | 无需安装，供其它包调用 | — |
| 1 | `02-libscif` | 用户态 SCIF 库 `libscif.so` + `scif.h` / `scif_ioctl.h` | `/usr/lib64`、`/usr/include` | 00 |
| 2 | `03-mpss-daemon` | `libmpssconfig.so`、主机守护进程 `mpssd`、管理 CLI `micctrl`（setuid root） | `/usr/lib64`、`/usr/sbin`、`/usr/include/mic` | 02 |
| 3 | `04-mpss-micmgmt` | `libmicmgmt.so`、`mpssinfo`、`mpssflash`、`micsmc` | `/usr/lib64`、`/usr/bin`、`/usr/include` | 02 |
| 4 | `05-miccheck` | 自检工具 `miccheck`（Python 3 版） | `/usr/bin`、`/usr/src/miccheck` | 04 |
| 5 | `06-mpss-coi` | offload 库 `libcoi_host.so`（公开 ABI 名 `@@COI_1.0`） | `/usr/lib64`、`/usr/include/intel-coi` | 02 |
| 6 | `07-mpss-myo` | offload 库 `libmyo-client.so`（公开 ABI 名 `@@MYO_1.0`） | `/usr/lib64`、`/usr/include` | 02 |
| 7 | `08-mic-module` | 主机内核模块 `mic.ko` + modprobe/udev 配置 + 内核头 | `/lib/modules/$(uname -r)`、`/etc`、`/usr/include/mic` | — |
| 8 | `09-boot-images` | 卡端引导镜像 `bzImage-knightscorner`、`initramfs-knightscorner.cpio.gz` | `/usr/share/mpss/boot` | — |

## 二、逐包安装

　　每个包目录里有两个 Makefile：`Makefile.mpss` 是上游原始文件（已含移植补丁），`Makefile` 是本发布版的包装（构建 + 按 MPSS 期望的路径安装）。**主用法就是进目录执行 `make install`**：

```bash
cd 02-libscif      && sudo make install
cd ../03-mpss-daemon && sudo make install
cd ../04-mpss-micmgmt && sudo make install
cd ../05-miccheck  && sudo make install
cd ../06-mpss-coi  && sudo make install
cd ../07-mpss-myo  && sudo make install
cd ../08-mic-module && sudo make install
cd ../09-boot-images && sudo make install
```

　　三条通用规则：

- **暂存安装**：任何包都支持 `make install DESTDIR=/tmp/stage`，此时不会运行 `ldconfig`/`depmod`，也不需要 root。
- **改安装前缀**：`make install PREFIX=/usr/local`（默认 `/usr`）。注意 `micctrl` 会装成 setuid root，`DESTDIR` 与 `PREFIX` 的组合要自己保证合理。
- **内核模块包**需要内核头文件：默认取 `/lib/modules/$(uname -r)/build`，可用 `make install KERNEL_SRC=...` 指定。
- **建议两步走**：先以普通用户 `make`，再 `sudo make install`。直接 `sudo make install` 会以 root 身份完成构建，在源码树里留下 root 属主的目标文件；之后若再以普通用户重新构建，会因写不进去而报 `Permission denied`（表现为 `can't create objs/xxx.o`），此时先 `sudo make clean` 或 `sudo chown -R $USER .` 即可。

　　顶层还有一个便利 `Makefile`，按上表顺序一次装完（`sudo make install`，约 150 秒）；调试单包时仍建议逐个安装。

## 三、装完之后

### 3.1 生成配置与卡镜像目录

```bash
sudo micctrl --initdefaults
```

　　这一步会在 `/etc/mpss/` 生成 `mic0.conf`，在 `/var/mpss/mic0/` 生成卡端文件系统目录（MicDir），里面已经包含 `etc/passwd`（主机普通用户会被合并进去）、`etc/network/interfaces`（卡端网口）、`etc/ssh` 主机密钥、各用户 `.ssh/authorized_keys`。

### 3.2 按本机情况改两行

```bash
sudo sed -i 's|^Network .*|Network class=StaticPair micip=171.31.1.2 hostip=171.31.1.1 netbits=24 modhost=no modcard=yes mtu=64512|' /etc/mpss/mic0.conf
sudo sed -i 's|^BootOnStart .*|BootOnStart Enabled|' /etc/mpss/mic0.conf
```

　　`modhost=no` 表示主机侧网口由你自己配（推荐，避免 MPSS 去改发行版的网络配置）；`modcard=yes` 表示卡端网络配置由 MPSS 写进 MicDir。IP 按你的实际网段改。

### 3.3 起守护进程

　　用 systemd 时，写一个 `Type=simple` 的单元（**不要用 `Type=forking`**：`mpssd` 默认 fork 之后父进程会 `pause()` 永不退出，`forking` 必然超时）：

```ini
# /etc/systemd/system/mpss.service
[Unit]
Description=Intel(R) MPSS control service (LoongArch port)
After=network.target

[Service]
Type=simple
ExecStart=/usr/sbin/mpssd -l
TimeoutSec=60

[Install]
WantedBy=multi-user.target
```

```bash
sudo systemctl daemon-reload && sudo systemctl enable --now mpss
```

　　不用 systemd 时直接 `sudo /usr/sbin/mpssd -l &` 亦可（`-l` 是前台、日志到屏幕）。

### 3.4 主机侧网口

```bash
sudo ip addr add 171.31.1.1/24 dev mic0 && sudo ip link set mic0 up
```

　　`mic0` 由驱动在卡进入 `online` 时创建（MTU 64512）。

## 四、验证

```bash
micctrl --status                 # 期望：mic0: online (mode: linux image: ...)
mpssinfo                         # 读卡的 SKU／序列号／核数／温度／Flash 版本等
miccheck                         # 自检；全绿时输出 Status: OK
ssh root@171.31.1.2              # 卡的地址；卡端镜像已含授权密钥
```

　　用 MPSS 自己的工具在卡上建用户并注入密钥（这是 `micctrl` 的完整管理路径，控制器经 SCIF 与卡上 `mpssd` 通信）：

```bash
sudo micctrl --useradd=<用户名>   # 主机侧 MicDir 与运行中的卡同时生效
```

　　验收点：卡端 `/var/log/mpssd` 出现 `[UserAdd] '<用户名>' Success`，随后可用该用户 `ssh` 进卡。

## 五、这份移植版改了什么

　　上游 2016 年的代码在今天的工具链上有两类问题：**架构耦合**（x86 内联汇编、SSE2）与**工具链变严**（新 binutils 对 `.symver` 的限制、GCC 10 的 `-fno-common`、GCC 14 把隐式函数声明与指针类型不兼容升为错误）。逐条改动与位置见随附报告附录 E，要点：

| 包 | 主要改动 |
|---|---|
| `03-mpss-daemon` | 发行版探测补 AOSC／LoongArch 分支（否则环境初始化直接失败）；修 `parse_shadow` 里原代码自带的未定义行为（`*lastd[1]` 应为 `(*lastd)[1]`，在 x86 上碰巧不炸）；`getcwd() < 0` 改 `== NULL`；卡端 SSH 主机密钥由 `rsa1`/`dsa` 改为 `ed25519`/`rsa`/`ecdsa` |
| `04-mpss-micmgmt` | 去掉 `inline` 伪声明（只声明不定义，GCC 14 起是错误）；覆盖 `EXTRA_CFLAGS` 里的 `-Werror` |
| `05-miccheck` | Python 2 → 3（`except X, e:`、shebang、`communicate()` 返回 bytes、`create_string_buffer().value` 是 bytes）；外部命令路径写死 `/sbin/…` 时回退查 PATH |
| `06-mpss-coi` | 构建分支补 `WHAT_loongarch64 := HOST`；`cpuid`／`rdtsc` 换成等价实现（`sched_getcpu()`／`CLOCK_MONOTONIC`）；非 x86 不发射 `.symver`，公开 ABI 名改由链接期别名生成 |
| `07-mpss-myo` | `INTEL64` 推导补龙芯；`rdtsc` 换单调时钟；`lock; xaddl` 换 `__atomic_add_fetch`；`popcount32`/`nlz32` 无条件定义；信号上下文里的 x86 `REG_ERR` 按「写」处理；SSE2 差分层改用源码自带的标量回退（`-DMYOI_DIFF_I64`） |
| `08-mic-module` | 沿用随附报告第三、六章记录的内核模块移植（`MAX_ORDER`、`del_timer_sync`、`from_timer`、`get_user_pages`、`tty_alloc_driver` 等一批接口更新） |
| `09-boot-images` | 卡端 initramfs 已补 `auto mic0` 静态网口配置与授权密钥（原交付里没有，MPSS 正常流程是由主机侧 `mpssd` 下发的） |

## 六、已知问题与注意事项

1. **特权边界**：读 sysfs 的功能（设备枚举、SKU、POST 码、Family/Model）普通用户即可；走 SCIF 的功能（序列号、UUID、显存/核心信息、温度、RAS）要先打开 `/dev/mic/scif`，该设备默认 `crw------- root root`。这也是 `micctrl` 装成 setuid root 的原因。
2. **卡端 mpssd 的握手时序**：卡端 `mpssd` 启动时先连主机 mpssd 的 160 端口发 `MONITOR_START`，**只有握手成功后才创建监听端口 164 的线程**。所以主机 `mpssd` 必须在卡端启动那一刻正在运行；若卡是先于 mpssd 手工引导的，在主机 mpssd 运行后重启一次卡端 mpssd 即可补上。不补的后果是所有走 164 端口的操作（如 `micctrl --useradd`）会连到不存在的监听者。
3. **失败的 SCIF 连接会在内核里无限重试**：`micscif/micscif_api.c` 的连接等待循环在对端不应答且设备仍存活时会 `goto retry`，进程不返回用户态，`SIGTERM`／`SIGKILL` 都无法终止；只能靠重置卡或卸载模块让它退出。这是上游代码的行为，未在移植中修改。
4. **`System.map` 是占位空文件**：没有卡内核的符号表，`mpssd` 会打一条 `mmap of System.map failed` 告警，不影响引导。
5. **手册页**：`.1`／`.3` 等 man 页需要 `a2x`（asciidoc）生成，包装 Makefile 不构建它们。
6. **`05-miccheck` 的版本号是构建期常量**：`make` 时通过 `MPSS_FLASH_VERSION`／`SMC_FW_VERSION` 写入；默认值取自实测的卡（flash `391`、SMC `1.17.6900`）。换卡后如自检报版本不匹配，用新的值重跑 `make install` 即可。

## 七、卸载

　　各包的产物路径都在上面第一节的表里，删除对应文件即可；内核模块用 `sudo rm -rf /lib/modules/$(uname -r)/extra/mic.ko*`（或 `updates/`，取决于内核版本）并 `sudo depmod -a`。
