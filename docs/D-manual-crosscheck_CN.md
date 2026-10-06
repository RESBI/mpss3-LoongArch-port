# 附录 D　手册核对：Intel 的两份手册能背书什么

> 　本附录把报告里每一句「手册说」的判断落到行号上，行号一律指抽取件 `_work/pdf/mpss_users_guide.txt` 与 `_work/pdf/knc_isa_manual.txt` 的行号。抽取件保留了手册的分页标记（形如 `===== [mpss_users_guide p.40] =====`），所以一句引用可以同时定位到抽取行与手册页码。反过来，**手册里查不到的东西也在这里点清** —— 那正是第七章末尾那张「未能核实」清单的成因。

---

## D.1　两份抽取件分别是什么

| 抽取件 | 行数 | 是哪份文档 | 报告里谁在用 |
|---|---:|---|---|
| `_work/pdf/mpss_users_guide.txt` | 8,241 | Intel MPSS 3.8 User's Guide，2017 年 4 月 | 第一章 §1.3、第二章（BAR 与 `lspci` 抄本）、第三章 §3.5（模块参数）、附录 A.5 |
| `_work/pdf/knc_isa_manual.txt` | 21,487 | Intel Xeon Phi 协处理器指令集手册 | 第五章 §5.3（指令集那一关）、本附录 D.5 |

　　两份都不是硬件手册：用户指南是**装机与排障手册**，ISA 手册是**卡上那颗 CPU 的指令集手册**。这条边界决定了它们的用法 —— 手册只用来给「卡是什么样」背书；凡涉及主机内核行为、PCI 枚举、地址分配、中断分配的，一律回到源码与上游内核，因为手册里根本没有这些内容（逐条统计见 D.4）。

---

## D.2　手册能给报告背书的七条

| 手册行 | 手册里的话 | 它背书了哪一句 | 报告位置 |
|---|---|---|---|
| `:1098`–`:1100` | `BIOS and OS support for large (8GB+) Memory Mapped I/O Base Address Registers (MMIO BAR's) above the 4GB address limit must be enabled.` | 8 GiB 高位窗口是**固件开关**，不是内核自动就能要到 | 第一章 1.5、第七章 7.11 的 P0-A、第十一章第 1 步 |
| `:1359`–`:1362` | `Region 0: Memory at 3c7e00000000 (64-bit, prefetchable) [size=200000000]` 与 `Region 4: Memory at ec000000 (64-bit, non-prefetchable) [size=128K]` | 卡存窗口是 8 GiB 且远在 4 GiB 之上；寄存器窗口是 128 KiB | 第二章 2.2 的 BAR 表、附录 A.1 |
| `:1370` | `The output shows that both BAR0 (region 0) and BAR1 (region 4) have valid assigned values.` | **术语警告**：手册的「BAR1」指的是配置空间里的 region 4，与 `pci_resource` 的序号不是一回事 | 第一章术语、附录 A.1 的宏表 |
| `:1497`–`:1506` | `micinfo -group Board` 的输出：`PCIe Width: x16`、`PCIe Speed: 5 GT/s`、`Max payload size: 256 bytes`、`Max read req size: 512 bytes` | 卡是 x16 Gen2 设备，最大载荷 256 字节 —— 8 GiB 窗口要靠它搬 | 第二章 2.2 |
| `:4943`–`:5053` | 八个小节逐条解释 `watchdog`、`watchdog_auto_reboot`、`crash_dump`、`p2p`、`p2p_proxy`、`ulimit`、`reg_cache`、`huge_page` | `mic.conf:31` 那行里的八个参数，手册里**每个都有对应小节**，参数语义以手册为准 | 第三章 3.5 的参数表 |
| `:3027`–`:3030` | `The augmented command line can be read at /sys/class/mic/micN/kernel_cmdline.` 并列出 `card`、`vnet`、`scif_id`、`scif_addr`、`mem`、`ramoops_size` 等条目，说明它们由 `mic.ko` 自动补齐 | 附录 A.5 的契约五：那串参数是写给卡上内核的，不是写给主机的 | 附录 A.5、第三章 3.5 |
| `:8041`–`:8070` | `cat /sys/class/mic/micN/post_code` 与 POST 码表：`11 Signal host to download coprocessor OS`、`12 Wait for coprocessor OS download`、`13 Signal received from host to boot coprocessor OS` | 职责切分是**硬件层面的事实**：卡自己走到「等主机灌镜像」这一步，主机只负责灌与敲门 | 第二章 2.5、附录 A.5 的流程图 |

　　POST 码 `11`／`12`／`13` 这三步，是 Intel 自己文档化的交接点。报告全篇「主机只管灌镜像、敲门铃、收中断」的说法，依据就在这里加上第三章的源码。

---

## D.3　sysfs 的逐项对照：手册列的 31 个节点，源码一个不少

　　用户指南的「host driver sysfs entries」一节（`_work/pdf/mpss_users_guide.txt:7205` 起）列出 31 个 `/sys/class/mic/micN/*` 节点。把它们逐个拿回 `host/linsysfs.c` 对照，结果是**一个不少**：

| 节点 | 手册 | 源码 |
|---|---|---|
| `active_cores` | `:7214` | `host/linsysfs.c:119` |
| `boot_count` | `:7266` | `host/linsysfs.c:442` |
| `cmdline` | `:2827` | `host/linsysfs.c:487` |
| `crash_count` | `:7267` | `host/linsysfs.c:452` |
| `extended_family` | `:7322` | `host/linsysfs.c:115` |
| `extended_model` | `:7323` | `host/linsysfs.c:114` |
| `fail_safe_offset` | `:7305` | `host/linsysfs.c:120` |
| `family` | `:7206` | `host/linsysfs.c:228` |
| `flash_update` | `:7304` | `host/linsysfs.c:161` |
| `flashversion` | `:7303` | `host/linsysfs.c:105` |
| `fuse_config_rev` | `:7324` | `host/linsysfs.c:117` |
| `image` | `:7228` | `host/linsysfs.c:396` |
| `kernel_cmdline` | `:3027` | `host/linsysfs.c:502` |
| `log_buf_addr` | `:7276` | `host/linsysfs.c:641` |
| `log_buf_len` | `:7277` | `host/linsysfs.c:671` |
| `meminfo` | `:7325` | `host/linsysfs.c:174` |
| `memoryfrequency` | `:7331` | `host/linsysfs.c:103` |
| `memoryvoltage` | `:7332` | `host/linsysfs.c:102` |
| `memsize` | `:7215` | `host/linsysfs.c:104` |
| `mode` | `:7227` | `host/linsysfs.c:374` |
| `model` | `:7333` | `host/linsysfs.c:110` |
| `pc3_enabled` | `:7312` | `host/linsysfs.c:538` |
| `pc6_enabled` | `:7313` | `host/linsysfs.c:576` |
| `platform` | `:7273` | `host/linsysfs.c:113` |
| `post_code` | `:7274` | `host/linsysfs.c:432` |
| `scif_status` | `:7275` | `host/linsysfs.c:388` |
| `sku` | `:7207` | `host/linsysfs.c:182` |
| `state` | `:2830` | `host/linsysfs.c:361` |
| `stepping` | `:7208` | `host/linsysfs.c:238` |
| `stepping_data` | `:7335` | `host/linsysfs.c:109` |
| `virtblk_file` | `:5155` | `host/linsysfs.c:711` |

　　反方向并不对称：`host/linsysfs.c` 里另有 11 个属性，手册该节没有列 —— `corevoltage`（`:99`）、`corefrequency`（`:100`）、`substepping_data`（`:108`）、`family_data`（`:111`）、`processor`（`:112`）、`version`（`:190`）、`peer2peer`（`:197`）、`initramfs`（`:404`）、`pc6_timeout`（`:611`）、`serialnumber`（`:697`）、`interface_version`（`:704`）。其中 `peer2peer` 其实有文档，只是挂在另一个类路径下（`_work/pdf/mpss_users_guide.txt:7196` 的 `/sys/class/mic/ctrl/peer2peer`）。

　　这张对照表的作用是确定**移植时的改动边界**：这 31 个名字是主机用户态（`micctrl`、`mpssd`）与运维脚本要看的东西，名字改了就等于改 ABI。附录 A.8 把它们冻结，本附录给出每一个名字的出处。

---

## D.4　手册的空白：第七章那些「未能核实」不是没查，是查不到

　　报告在第七章列了若干「未能核实」，其中最容易挨问的是这一类：**为什么不去查手册。**答案在下面的统计里 —— 按关键词在用户指南全文检索：

| 关键词 | 手册命中 | 意味着 |
|---|---:|---|
| `doorbell` | 0 | 主机怎么敲门铃，手册一个字没讲 |
| `aperture` | 0 | 8 GiB 窗口怎么映射到主机地址空间，手册没讲 |
| `MSI-X` | 0 | 用几个中断向量、能不能拿到，手册没讲 |
| `IOMMU`、`VT-d` | 0 | DMA 一致性、地址位宽，手册没讲 |
| `cpuid`、`wbinvd` | 0 | 主机特权指令，手册没讲（它讲的是卡的指令集） |
| `page size`、`4 KB` | 0 | 主机页大小，手册只在 `:5043` 用 `4K` 写过一次（见 D.6） |
| `PCIe` | 24 | 但全部是速率、宽度与 BIOS 开关，没有枚举、BAR 分配、桥窗口 |

　　结论有两句：第一，**手册是用户手册，不是硬件手册**，报告里凡涉及 PCI 枚举、地址分配、中断分配的判断，出处只能是源码与上游内核；第二，报告之所以在第七章把「固件给不给 8 GiB 高位窗口」写成可能否决项而不是写成结论，就是因为这句话在手册里只能查到「BIOS 必须打开这个开关」（`:1098`），**查不到「这块主板到底给不给」**。

---

## D.5　ISA 手册在这里的位置

　　抽取件 21,487 行，全文 `K1OM` 命中 **0** 次、`Knights Corner` 命中 **0** 次 —— 它自称的是 Intel Xeon Phi 协处理器指令集（目录见 `_work/pdf/knc_isa_manual.txt:46`–`:55`），讲的是**卡上那颗 CPU** 的 512 位向量扩展（`:568` 起：32 个 512 位向量寄存器）。

　　因此它对本次施工的直接贡献是零：卡侧 24 个文件、20,232 行一行不改（附录 B.3），主机侧唯一的 x86 汇编是 `micscif/micscif_ports.c:150`、`:177`、`:196` 三行，指令是 `bsfq`／`btrq`／`btsq` —— 恰好是 ISA 手册不覆盖的主机指令。

　　`micscif/micscif_ports.c:129` 的条件是 `#if 1 && (defined(__GNUC__) || defined(ICC))`，**它不是架构判断**，所以在龙芯上仍然会走这三行内联汇编。这就是第五章 F6 那句「编译即断」的确切含义 —— 属于最容易修的一类（换成内核的 `ffs()`／`__clear_bit()`／`__set_bit()` 即可），但绝不能因为看到 `#if` 就以为它不编。

　　ISA 手册真正能对上号的是卡侧代码，例如卡上 AP 的 APIC 与 CPUID 约定（`_work/pdf/knc_isa_manual.txt:2206`、`:19903`、`:19920`）。那属于「卡怎么启动」的分析，不属于本次主机侧移植。

---

## D.6　手册里唯一提到页大小的一句，以及它牵出的一个节点

　　用户指南 `_work/pdf/mpss_users_guide.txt:5040`–`:5043` 原文：

```text
[host]# echo <limit> > /proc/scif/reg_cache_limit
where <limit> is the decimal number of 4K pages.
```

　　这是整份手册里唯一一句与主机页大小有关的话，而它描述的节点在源码里是这么实现的：目录 `micscif/micscif_debug.c:873`，节点注册在 `:880`（`proc_create`）与 `:903`（旧接口 `create_proc_entry`），默认值取 `include/mic/micscif.h:110` 的 `SCIF_RMA_TEMP_CACHE_LIMIT`（`0x20000`），在 `host/linscif_host.c:128` 与 `micscif/micscif_main.c:381` 分别灌进宿主的 `ms_info.mi_rma_tc_limit`，而**比较它时用的是主机页位移**：

```c
	if ((cur_bytes >> PAGE_SHIFT) > ms_info.mi_rma_tc_limit)
```

　　[`micscif/micscif_rma.c:1471`]，同一判断在 `:1474` 再出现一次。

　　把数字摊开：`0x20000` 是 131,072 个页，在 4 KB 页上等于 **512 MiB**，在 16 KB 页上等于 **2 GiB**。也就是说，宿主页大小一改，这个**已经写进手册、也已经暴露在 `/proc` 上**的节点，单位与实际预算同时被放大四倍：手册告诉用户单位是 4K 页，而代码按宿主页算。

　　处置与第七章 §7.2 的决定一致：宿主内核按 4 KB 页构建（`CONFIG_PAGE_SIZE_4KB`），这一项连同 F3、F4 一起继续按「潜在」处理；若部署内核取 16 KB 默认值，那么除了 F3 之外，还要重新定义这个节点的单位 —— 它不是崩溃，是**接口语义漂移**，属于必须在上机验证里点名的那一类（第十一章 §11.4）。

---

## D.7　复核命令

```powershell
$g = "_work\pdf\mpss_users_guide.txt"
Select-String -Path $g -Pattern 'doorbell|aperture|MSI-X|IOMMU|VT-d|cpuid|wbinvd'   # 全部应为空
Select-String -Path $g -Pattern 'Region 0|Region 4|size='                        # 命中 1359-1362 那段 lspci 抄本
Select-String -Path $g -Pattern '4K pages'                                       # 只应命中 5043 一行
Select-String -Path "_work\pdf\knc_isa_manual.txt" -Pattern 'K1OM|Knights Corner'   # 全部应为空
Select-String -Path "_work\mpss-modules-3.8.6\host\linsysfs.c" -Pattern 'DEVICE_ATTR' -CaseSensitive # 数出 43 行（不加 -CaseSensitive 会命中 83 行）
```

　　最后一条要这样读：`host/linsysfs.c` 里 `DEVICE_ATTR` 共命中 43 行，其中 `:84` 一行是这个文件自己定义的 `DEVICE_ATTR_SBOX` 宏（它带 `_name` 这个形参），其余 42 行就是 42 个属性定义 —— 25 个普通属性（`bd_attributes[]` 23 条加 `host_attributes[]` 2 条）与 17 个 SBOX 属性。这 42 个里，手册列了 31 个、另有 11 个没列，31 + 11 = 42。43 = 1 + 42 是一笔平账，附录 A.7 里那张名字表也是这么分的。
