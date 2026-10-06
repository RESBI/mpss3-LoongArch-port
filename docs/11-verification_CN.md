# 第十一章　上机验证清单

> 　前面十章一直在纸上论证。这一章把它变成一台真机上可以照着做的动作序列：**先测什么、看哪个打印、什么算通过、不通过时先怀疑谁。**排在前面的是「不通过就不能做」，排在后面的是「不通过只是不好用」。

---

## 11.1　验证顺序的排序原则

　　原则只有一条，与第五章和第七章一致：**先测会否决整件事的，再测会静默算错的，最后测只是不好用的。**据此把全部验证项分成三类：

| 类别 | 判据 | 不通过的后果 | 本步骤 |
|---|---|---|---|
| **否决项** | 平台资源是否够 | 整个项目做不下去 | 11.2、11.3、11.4 |
| **静默项** | 会不会算错而不报错 | 能跑，但数据是错的 | 11.5、11.6 |
| **功能项** | 卡能不能用 | 能用多少的区别 | 11.7 |

　　第七章 7.12 已经给过一份「六步最小验证」的**设计**。本章是它的**执行版**：补上具体的观测命令、具体的打印文本、具体的判据，并把顺序按「失败有多贵」重排。两份不冲突，本章更细。

---

## 11.2　否决项一：固件给不给 8 GiB 的高位可预取窗口

　　**这是整份报告的头号判据。**

　　第一步是**从设备自己嘴里问**：

```bash
lspci -vvv -s <卡的 BDF>
```

　　判据有两条，缺一不可：

| 编号 | 看哪一行 | 通过判据 |
|---|---|---|
| A1 | `Region 0:` | 尺寸必须是 **8 GiB**（形如 `[size=8G]` 或 `[size=200000000]`），且地址**位于 4 GiB 以上** |
| A2 | `Region 0:` 的属性串 | 必须同时含 `64-bit` 与 `prefetchable`；`Region 4:` 必须是 `64-bit, non-prefetchable` 且大小为 128 KiB |

　　MPSS 用户指南里的实机抄本正好是这个形状，可以拿来比对：

```
Region 0: Memory at 3c7e00000000 (64-bit, prefetchable) [size=200000000]
Region 4: Memory at ec000000 (64-bit, non-prefetchable) [size=128K]
```

　　这两行的**对比关系**是：BAR4 只要 128 KiB，落在 4 GiB 以下也毫无问题；而 BAR0 要 8 GiB，x86 主桥必然把它放到 4 GiB 以上。**「固件有没有那个把大 BAR 挪到高位的能力」这个问题，实际就是问 BAR0 会落在哪里。**要是 BAR0 报出来的尺寸不是 8 GiB，就直接停在这里——第七章 7.4 已经确认，上游没有任何等价于 x86「Above 4G Decoding」的东西可以打开。

　　第二步是**问内核有没有真的把它分出去**：

```bash
sudo dmesg | grep -i -E 'pci|BAR'
cat /proc/iomem | grep -i -A2 mic
```

　　通过的判据是：`/proc/iomem` 里出现一块**连续 8 GiB** 的 `mic` 条目。若 `request_mem_region` 没拿到，驱动会打印：

```
mic 0: failed to reserve aperture space
```

　　[`host/linux.c:303`]。看到这一行，就是 11.2 没过，不必往下走。

---

## 11.3　否决项二：一致性 DMA 掩码有没有被固件收窄

　　第七章 7.6 已确认：龙芯的 `acpi_arch_dma_setup()` 会读固件的 `_DMA`，然后对 `*dev->dma_mask` 与 `dev->coherent_dma_mask` **各取一次最小值**。而驱动的流式掩码**只要 64 位，没有回退**：

```c
	err = pci_set_dma_mask(pdev, DMA_BIT_MASK(64));
	if (err) {
		printk("mic %d: ERROR DMA not available\n", brdnum);
		goto probe_freebd;
	}
```

　　[`host/linux.c:274`–`278`]

　　### 测量方法里有一个必须先做的动作

　　**探针模块必须在 `mic.ko` 之前加载。**原因是 `pci_set_dma_mask()` 会**改写** `dev->dma_mask`。一旦 `mic.ko` 先加载，你测到的就是它改写之后的值（`0xFFFFFFFFFFFFFFFF`），而不是固件给的初值——那样测出来的永远是「通过」。

　　探针模块只需二十来行，做三件事：

1. 用 `pci_get_device()` 找到设备；
2. `pci_enable_device()`，然后**只读不写**：打印 `*pdev->dev.dma_mask`、`pdev->dev.coherent_dma_mask`、`pdev->dev.bus_dma_limit`；
3. 打印 `dma_get_required_mask(&pdev->dev)`。

　　辅助手段是直接读固件的 ACPI 表，看它有没有为这个设备写 `_DMA`。

　　判据统计如下：

| 观测结果 | 结论 |
|---|---|
| 三个值全是 64 位 | 11.3 通过，进入 11.4 |
| `bus_dma_limit` < 64 位 | **P0-B 命中**。此时有两种可能的结局（第七章 7.6 已说明）：probe 直接报 `mic 0: ERROR DMA not available`，或者 probe 过了但 DMA 被静默限制在 `bus_dma_limit` 之内。前者会响，后者不会——所以这一条必须查清，不能靠「模块加载成功」作为通过判据 |
| 一致性掩码是 32 位 | 会打印 `mic 0: ERROR pci_set_consistent_dma_mask(64) %d`，然后**退到 32 位重试一次**（`host/linux.c:279`–`287`）。它可能仍然通过，但意味着卡的显存只能落在 4 GiB 以下 |
| 流式掩码是 32 位 | 打印 `mic 0: ERROR DMA not available`，probe 终止 |

---

## 11.4　否决项三：页大小

　　观测点只有一个：部署内核的 `CONFIG_PAGE_SIZE_*`。

```bash
zcat /proc/config.gz | grep -E 'PAGE_SIZE|HAVE_PAGE_SIZE'
```

　　通过判据：配置为 **4 KB 页**。若内核只能用 16 KB，影响面是**有限且可枚举**的，这一点第五章 5.5 已经逐处判定：

| 编号 | 位置 | 16 KB 页下的后果 | 严重度 |
|---|---|---|---|
| **F3** | `include/mic/micpsmi.h:56`–`57` | `MIC_PSMI_PAGE_SIZE` 从 512 KB 变成 2 MB，PSMI 页表粒度跟着变 | **高**。这类页表如果卡侧按 512 KB 期望读，就会错 |
| **F4** | `host/uos_download.c:344`–`349` | GTT 表项用主机页号 | **在本平台上不触发**：`GTT_WRITE` 全树只有一个调用点，属于 `FAMILY_ABR` 那条分支，KNC 不走它。这是第五章里被修正过的一处判断 |
| **H2** | `include/mic/micscif.h:124`、`include/mic/mic_dma_md.h:87` | 强行 `#define L1_CACHE_SHIFT 6` | 低。龙芯 L1 缓存行恰好也是 64 字节，这不是问题 |

　　所以 16 KB 页下的真实欠账只有 **F3 一条**，而它可以按第五章 5.5 的办法把 `PAGE_SIZE` 换成固定 512 KB 粒度、与主机页解耦。**页大小是风险，不是否决项。**

　　F3／F4／H2 之外还要在清单上加一条**口径漂移**（不是耦合，量它的目的和上面三条不同）：SCIF 的注册缓存上限。用户指南把 `/proc/scif/reg_cache_limit` 的取值定义成「以 4 KB 页计的十进制数」（`_work/pdf/mpss_users_guide.txt:5043`），而代码把它当页数比较（`include/mic/micscif.h:110` 的 0x20000 配合 `micscif/micscif_rma.c:1471`、`:1474` 的 `cur_bytes >> PAGE_SHIFT`）。于是这个上限在 4 KB 页下是 512 MiB、16 KB 页下是 2 GiB —— 代码不会算错，量它是因为**换了页大小以后，同一个数值的物理含义和手册上说的不一样**，调参的人如果照手册理解就会调偏四倍。上机时读一次 `/proc/scif/reg_cache_limit`，连同部署内核的页大小一起记进验证记录即可，分析见[附录 D](D-manual-crosscheck_CN.md) §D.6。

---

## 11.5　静默项一：平台是否把这个设备当 I/O 一致

　　这是全报告**唯一的正确性风险**，必须单独量。理由在第七章 7.6 已经列出：驱动里**零** `dma_alloc_coherent`、**零** `dma_sync_single_*`，唯一的同步动作是 `dma/mic_dma_lib.c:417` 的一处 `wmb()`。它对缓存一致性的全部依赖，就是「平台会把这个 PCIe 设备声明成 I/O 一致」。

　　先做便宜的检查：

```bash
ls /sys/class/iommu            # 应为空
ls /sys/bus/pci/devices/<BDF>/  # 应无 iommu_group
dmesg | grep -i iommu           # 应无相关输出
```

　　但这三条只能证明「没有 IOMMU」，**不能**证明「设备是 I/O 一致的」。后者只能这样量：

　　往卡里灌一个**内容已知**的镜像或数据块，再让卡把这段内容按同一个布局读回主机，逐字节比对。这不是性能测量，是一次数据往返的比对；它一旦不对，前面所有代码都不用再调。

---

## 11.6　静默项二：`ioremap_wc()` 实际落到哪种属性

　　这一步之前必须先做一件事：**确认 `writecombine=on` 已经生效**。龙芯 6.6 内核的 `CONFIG_ARCH_WRITECOMBINE` 默认关闭，关着的时候 `ioremap_wc()` 会静默退化成 SUC（`v6.6/arch/loongarch/Kconfig:479`–`:493`、`v6.6/arch/loongarch/kernel/setup.c:171`–`:182`，详见[第七章](07-loongarch-platform_CN.md) §7.7）。不加这个参数，下面第 2 步测到的其实是 SUC，结论会反过来。观测办法是比较：

1. 用 `writecombine=on` 启动，对同一段设备内存用 `ioremap()` 映射，测顺序写吞吐；
2. 对同一段用 `ioremap_wc()` 映射，**确认引导参数生效**后测同一件事。

　　若加了 `writecombine=on` 之后两者**仍然没有可测的差别**，就说明这块桥上的 WUC 确实不可用（与 `v6.6/arch/loongarch/Kconfig:488`–`:491` 的判断一致），那么 8 GiB 卡存窗口的下载吞吐必须按强序非缓存重估。**这不是错误，是预算修正。**


　　这一步的**基线已经测到了**，可以省一轮上机：明文单流是卡到主机 31.0 与 49.5 MB/s、主机到卡 14.6 与 33.8 MB/s，四路并行至少有一轮到 172 MB/s（第八章 §8.6 的表）。这里要补的是 A/B —— 在引导参数里加上 `writecombine=on` 之后用同一套命令重测，比较两组数字。注意卡在空闲时会进 PC6 深睡，首包唤醒开销能到几百毫秒，两组测量要各测两遍以上再比，否则会把电源状态切换当成属性差异。

---

## 11.7　功能项：分三段上机

　　前面三项都过了，才轮到功能。

### 段一：射程甲（A + B + C 组，8,525 行）

　　动作：加载 `mic.ko`；用 MPSS 用户态完成初始化与引导。

　　通过的判据是一串具体的打印，它们在源码里都有出处：

| 看到 | 出自 | 含义 |
|---|---|---|
| `mic0: Transition from state ready to booting` | `include/mic_common.h:700` | 开始引导 |
| `/sys/class/mic/mic0/state` 读回 `online` | `host/linsysfs.c:240`、`:243` | 卡已正常起来 |
| `/sys/class/mic/mic0/post_code`、`boot_count` | `host/linsysfs.c:432`、`:442` | 可用于观察卡自检进度 |
| `mic0: Transition from state ready to reset failed` | `include/mic_common.h:700`、`:273` | 引导失败 |

　　`state` 属性的全部取值就是这十个：`ready`、`booting`、`no response`、`boot failed`、`online`、`shutdown`、`lost`、`resetting`、`reset failed`、`invalid`。

### 段二：射程乙（加 `linvcons`、`linvnet`、`vnet/*`）

　　动作：先量 10.7 提出的那个问题——**卡上的 SCIF 找不到主机对端时会不会优雅退让**。做法是先不加 SCIF 相关对象编译一份，看卡的 `mic0` 网络是否如期起来。

　　通过判据：主机上出现 `mic0` 网络接口，且卡侧能拿到地址、能通。

### 段三：射程丙（全量）

　　动作：把 `micscif/`、`dma/`、`host/vhost/`、`host/vmcore.c` 全部加回来。

　　通过判据：SCIF 能建立连接、能完成一次 RMA 传输；virtio 块设备能挂载；`vmcore` 能在卡崩溃后导出一份可用转储。

---

## 11.8　判据汇总

　　一张表，把上面所有判据收进来。**这张表就是本项目的验收标准。**

| 序 | 测什么 | 怎么测 | 通过 | 不通过 |
|---:|---|---|---|---|
| 1 | BAR0 尺寸与位置 | `lspci -vvv` | 8 GiB、64 位、可预取、> 4 GiB | 停 |
| 2 | BAR0 是否真被分到 | `/proc/iomem`、dmesg | 出现连续 8 GiB 的 `mic` 段 | 见 11.9 |
| 3 | BAR4 | `lspci -vvv` | 128 KiB、64 位、非预取 | 停 |
| 4 | DMA 掩码初值 | 先于 `mic.ko` 加载的探针模块 | 三个值都是 64 位 | 见 11.9 |
| 5 | 页大小 | `/proc/config.gz` | 4 KB | F3 需改写；不是否决 |
| 6 | 无 IOMMU | `/sys/class/iommu`、sysfs | 为空、无组 | 必须确认设备仍被声明为一致 |
| 7 | 缓存一致性 | 数据往返逐字节比对 | 完全一致 | 停 |
| 8 | 写合并是否生效 | **先加 `writecombine=on` 引导**，再比两种映射的写吞吐 | 有差别 | 性能重估，不阻塞 |
| 9 | 射程甲 | `micctrl` | `state` = `online` | 见 11.9 |
| 10 | 卡 SCIF 是否优雅退让 | 不加 `micscif` 编译运行 | 卡的 `mic0` 仍能起 | 射程乙作废，须做全量 |
| 11 | 射程丙 | SCIF 连接 + RMA | 成功 | 逐项排查 |

　　这张表只覆盖**上机**才能判定的部分。第五章那 11 条耦合（F1 至 F7、H1 至 H4）里，另有两类不需要在这里单列：

| 谁来抓 | 属于哪一类 | 结果 |
|---|---|---|
| 编译器（闸门二） | F1 的 `boot_cpu_data.x86_model`、F6 的 `bsfq`／`btrq`、F7 的 `slow_virt_to_phys()` 在龙芯内核里根本不存在 | 编不过就是抓到了，属于第八章 §8.3 的编译闭环，不需要另设判据 |
| 驱动自己的 printk | H1 的 64 位 DMA 掩码（`host/linux.c:274`–`278`）、两块 BAR 的 `request_mem_region` | 报错，回到 11.9 那张表 |
| 只有这张表能抓 | F3 的 PSMI 页粒度（16 KB 页下静默变大）、H3／H4 的写合并（`wc_enabled` 为假时静默走 SUC）、F5 的「无 IOMMU」前提 | **静默类，正是 11.4 与 11.6 存在的理由** |

　　换句话说：这张表里没有一项是「编译器会替我发现的」。编译器能发现的那几条在闸门二就会全部暴露，而这张表要抓的全是不报错的。

---

## 11.9　不通过时先怀疑谁

　　一旦不过，`dmesg` 的具体打印已经把故障点指给你了。照这张表查，不必从头读代码。

| dmesg 里出现 | 模块在哪一行 | 真正的病因 | 回到哪一步 |
|---|---|---|---|
| `mic 0: failed to reserve mmio space` | `host/linux.c:294` | BAR4 没被固件分到，或被别的驱动占用 | 11.2 |
| `mic 0: failed to reserve aperture space` | `host/linux.c:301`–`303` | BAR0 **没被分配**（`pci_resource_start`/`len` 为 0，`request_mem_region` 只能去撞 0 地址），或该段已被别的驱动占住。驱动是按报出来的实际 `pci_resource_len` 整块申请的，所以「窗口比 8 GiB 小」本身不会触发这一行 | 11.2 |
| `mic 0: ERROR DMA not available` | `host/linux.c:276` | 流式 DMA 掩码被固件压到 32 位 | 11.3 |
| `mic 0: ERROR pci_set_consistent_dma_mask(64)` | `host/linux.c:281` | 一致性掩码被压到 32 位（其后会退到 32 位重试一次） | 11.3 |
| `mic 0: failed to map aperture space` | `host/uos_download.c:1158`、`:1547` | 资源分到了，但 `ioremap` 8 GiB 失败 | 11.2 |
| `mic0: Transition from state ready to reset failed` | `include/mic_common.h:700` | 上面任一条的下游后果 | 按前五行定位 |
| 无任何 `Transition from state` 打印 | `include/mic_common.h:700` | 模块根本没加载成功 | 从 11.2 重新开始 |

　　最后一条判据最有用：**`mic_setstate()` 会在每一次状态迁移时都打印一行**。所以只要模块加载成功过一次，`dmesg` 里必然有它。一行都没有，说明问题在加载之前。
