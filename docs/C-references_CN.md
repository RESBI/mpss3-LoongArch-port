# 附录 C　引用与复核

> 　这份报告里的每一句话，都应当能被读者自己重新验证一次。本附录把散在各章里的出处集中起来，并给出可以直接照抄的命令。**复核不依赖任何外部网络资源**：报告里引用的上游内核路径，其对应版本的文件正文都已抓取到盘上，放在 `_work/.kcache/` 与 `_work/raw/` 两个目录里（见 C.2），所以 `v6.6/arch/x86/mm/pat/set_memory.c:763` 这类写法也能在工作目录里逐字定位。

---

## C.1　一手资料（全部在盘上）

| 路径 | 体量 | 是什么 | 主要用在哪 |
|---|---|---|---|
| `_work/mpss-modules-3.8.6/` | 104 个 `.c`／`.h` / 65,811 行（整树 124 个文件） | **Intel 原版 MPSS 3.8.6 内核模块源码树**（下称「甲树」） | 全报告，尤其三、五、六章 |
| `mpss-main/mpss-main/mpss3/mpss-modules/` | 与甲树同构 + 1 个补丁 | 社区树（下称「乙树」），已带到 RHEL 8 的 4.18 | 第六章的「现成先例」 |
| `mpss-main/mpss-main/mpss3/patches/` | 7 个补丁 | 见 C.3 | 第六章 |
| `_work/pdf/mpss_users_guide.txt` | 8241 行 | MPSS 用户指南的正文抽取，含 `lspci` 抄本与 BAR 尺寸 | 第二章、第七章 |
| `_work/pdf/knc_isa_manual.txt` | 21487 行 | K1OM 指令集手册正文抽取 | 第五章 5.3 |
| `_work/findings/x86-coupling.md` | 690 行 | 第五章的原始取证记录 | 第五章 |
| `_work/findings/platform.md` | 520 行 | 第七章的原始取证记录 | 第七章 |
| `_work/findings/users-guide.md` | 2169 行 | 用户指南的要点摘录 | 第二章、第四章 |
| `_work/findings/isa-manual.md` | 217 行 | ISA 手册的要点摘录 | 第五章 5.3 |
| `_work/findings/rpm-contents/` | 逐包文件清单 | 98 个 RPM 的内容盘点 | 第四章 |

　　甲树与乙树的差异只有那一个补丁。复核办法见 C.5 第五条。

　　两份 PDF 抽取文本与代码、报告的逐条对表集中在本报告附录 D：D.3 把用户指南记载的 31 个 sysfs 节点与 `host/linsysfs.c` 逐一对照，D.4 列出这两份手册在硬件层面没写的内容，D.6 追 `reg_cache_limit` 的单位漂移，D.7 给出对表的复现命令。手册类引用在全报告里统一写成 `_work/pdf/文件名.txt:行号`，可直接 `sed` 或 `read` 定位。

---

## C.2　审计中间产物（每一份都可以单独读）

　　这些文件是审计过程的**原始记录**，不是报告本身。报告里的每个数字都能在其中找到出处。C.1 与 C.2 两张表的「行」是同一个口径的实测值 —— 文件的行数（末尾若是一行没写完，也算一行），可自己用 `(Get-Content <文件>).Count` 逐个数一遍。

| 文件 | 体量 | 内容 |
|---|---:|---|
| `_work/host38-families.txt` | 224 行 | 38 个主机对象里，**逐族逐行**的可疑调用点（含 `file:line`） |
| `_work/host38-callsites.txt` | 122 行 | 同上，按内核接口族归类后的精简版 |
| `_work/kernel-api-hits.txt` | 392 行 | 全树范围内的内核接口命中（排除 `ras/`、`trace_capture/`） |
| `_work/dma-address.md` | 268 行 | 全树 DMA 地址用法的逐行判定 |
| `_work/findings/_treecount.txt` | 20 行 | 目录树的行数统计原始输出 |
| `_work/findings/_archscan.txt` | 267 行 | x86 耦合的机械扫描原始输出 |
| `_work/findings/_linsysfs.txt`、`_sysfs_hits.txt` | 171 行 | sysfs 属性清单（附录 A 的来源） |
| `_work/api-existence.md` | 195 行 | 14 组内核符号的旧版/6.6 存在性逐条核实 |
| `_work/api-versions-1/2/3.md` | 见文件 | 内核符号的**补丁级**版本判定（哪一版删除、6.6 的替代） |
| `_work/.kcache/` | 196 个文件 / 5,721,503 B，其中 190 个是钉住版本号的上游内核文件正文 | 全书 `vX.Y/路径:行号` 写法的原文出处。文件名把 `vX.Y/` 之后的斜杠换成下划线，例如 `v6.6/include/linux/sysfs.h` 存成 `v6.6__include_linux_sysfs.h` |
| `_work/raw/` | 117 个文件 / 7,596,800 B，其中 78 个带版本前缀 | 上游文件正文的另一批足本，文件名用双下划线分隔，例如 `v6.6__arch__loongarch__kernel__setup.c`。Bootlin Elixir 的 `slow_virt_to_phys` 标识符检索页也留在这里 |

---

## C.3　社区树的 7 个补丁

　　它们在 `mpss-main/mpss-main/mpss3/patches/`。前 5 个是 RHEL 7 的 3.10，后 2 个是 RHEL 8 的 4.18。

| 补丁 | 大小 | 行数 | 说明 |
|---|---:|---:|---|
| `mpss-modules-3.10.0-862.el7.x86_64.patch` | 1,380 B | 33 | RHEL 7.0 |
| `mpss-modules-3.10.0-957.el7.x86_64.patch` | 1,380 B | 33 | RHEL 7.6，与上一个同尺寸 |
| `mpss-modules-3.10.0-1062.el7.x86_64.patch` | 2,148 B | 50 | RHEL 7.7 |
| `mpss-modules-3.10.0-1127.el7.x86_64.patch` | 5,061 B | 134 | RHEL 7.8 |
| `mpss-modules-3.10.0-1160.el7.x86_64.patch` | 5,622 B | 146 | RHEL 7.9，RHEL 7 的最后一次 |
| `mpss-modules-4.18.0-193.el8.x86_64.patch` | 77,762 B | 2,372 | **RHEL 8.2：跨度最大的一次** |
| `mpss-modules-4.18.0-240.el8.x86_64.patch` | 77,762 B | 2,372 | RHEL 8.4，与上一个同尺寸 |

　　4.18 那两个补丁字节数相同，是同一份补丁的两个文件名。**它们是本报告第六章「现成先例」的全部依据**：它证明这套代码被社区从 3.10 带到了 4.18，跨了 4 个大版本。

---

## C.4　引用的上游内核源码

　　以下路径都在 Linux 主线里。报告引用它们，是为了说明**龙芯平台给了什么、以及什么已经被删除**。

| 路径 | 用在哪 | 引用了什么 |
|---|---|---|
| `arch/loongarch/Kconfig` | 七章 7.1 | 无条件 `select` 的清单 |
| `arch/loongarch/include/asm/page.h` | 七章 7.2 | 页大小的四种配置与 6.12 的重组 |
| `arch/loongarch/include/asm/addrspace.h` | 七章 7.3 | `PHYS_OFFSET`、映射窗口、`PCI_IOSIZE` |
| `arch/loongarch/include/asm/cache.h` | 五章 5.5 | `L1_CACHE_SHIFT` |
| `arch/loongarch/include/asm/barrier.h` | 七章 7.8 | `dbar 0x700` |
| `arch/loongarch/kernel/dma.c` | 七章 7.6 | `acpi_arch_dma_setup()` 的 `min()` 收窄 |
| `drivers/pci/controller/pci-loongson.c` | 七章 7.4 | LS7A 端口与 `non_compliant_bars` |
| `drivers/irqchip/Makefile` | 七章 7.5 | `irq-loongson-pch-msi.o` |
| `drivers/iommu/Kconfig` | 七章 7.6 | 没有龙芯条目；`IOMMU_DMA` 的依赖清单 |
| `drivers/vfio/Kconfig` | 十章 10.4 | `VFIO_NOIOMMU` 的原文声明「不支持虚拟机直通」 |
| `drivers/misc/mic/`（历史路径） | 七章 7.10 | v3.13 引入、v5.9 最后完整、**v5.10 删除** |
| `v6.4/include/linux/device/class.h` | 六章 | 6.4 起 `class_create()` 只剩一个参数 |

　　上游 MIC 驱动的删除提交是 **`80ade22c06ca115b81dd168e99479c8e09843513`**，标题「misc: mic: remove the MIC drivers」，作者 Sudeep Dutt，日期 2020-10-28，删掉 65 个文件、21,361 行。它是第七章 7.10 那张表的唯一依据。

---

## C.5　复核命令速查

### 第一条：射程是 38 个文件、32,746 行

```powershell
$r = "_work\mpss-modules-3.8.6"
$objs = Select-String -Path "$r\Kbuild" -Pattern '^mic-objs \+= (\S+)$' |
        ForEach-Object { $_.Matches[0].Groups[1].Value }
$objs.Count                                        # 应为 38
($objs | ForEach-Object { (Get-Content "$r\$($_ -replace '\.o$','.c'")").Count } | Measure-Object -Sum).Sum
                                                    # 应为 32746
```

### 第二条：卡侧与死代码不在射程内

```powershell
Select-String -Path "$r\Kbuild" -Pattern '^obj-' | ForEach-Object { $_.Line }
# 应看到 Kbuild:56-57 的 dma/ micscif/ ... 挂在 CONFIG_X86_MICPCI 下
# 以及 trace_capture/ 从未被任何一行引用
```

### 第三条：全树只有三行 x86-64 内联汇编

```powershell
Select-String -Path "$r\micscif\micscif_ports.c" -Pattern '__asm__|asm\(' 
# 应只命中 :150 :177 :196 三行，且都在 :129 的 #if 之内
```

### 第四条：全树没有任何 `dma_alloc_coherent`

```powershell
Get-ChildItem $r -Recurse -Include *.c,*.h |
  Select-String -Pattern 'dma_alloc_coherent|dma_map_single|dma_sync_single'
# 应只有一条：host\linpsmi.c:80 的 pci_dma_sync_single_for_cpu
```

### 第五条：乙树就是甲树

```powershell
$b = "mpss-main\mpss-main\mpss3\mpss-modules"
Get-ChildItem $r -Recurse -File | ForEach-Object {
  $q = Join-Path $b $_.FullName.Substring($r.Length + 1)
  ...
}
# 逐文件比对；差异应当是可以穷尽的，且已被 4.18 补丁覆盖
```

　　（上面这条是**逐文件比对**的思路。完整比对结果已经落在 `_work/diff-pristine-vs-latest.diff`，91,868 字节，可以直接读。）

### 第六条：`report/` 里的排版约定是否被破坏

```powershell
Get-ChildItem report -Filter *.md | ForEach-Object {
  $l = Get-Content $_.FullName
  "{0}: 单全角空格开头 {1} 行" -f $_.Name,
    ($l | Select-String -Pattern '^\u3000(?!\u3000)').Count
}
# 全部应为 0
```

### 第七条：页大小耦合的真实规模是 265 行、29 个文件

```powershell
$files = (Get-Content _work\host38-files.txt | ForEach-Object { "$r\$_.c" }) +
         (Get-Content _work\micsphdrs.txt   | ForEach-Object { "$r\$_" })
$hit = Select-String -Path $files -Pattern 'PAGE_SIZE|PAGE_SHIFT'
$hit.Count                                                   # 应为 265 行
($hit | Select-Object -ExpandProperty Filename -Unique).Count   # 应为 29 个文件
```

　　这两个数就是第五章 §5.5 那段口径说明的出处。§5.2 维度 4 写的是「40 个文件 368 处」——那是更宽的口径（含卡侧文件与不参与编译的分支），与这里的 265 不矛盾；两处说法已在 §5.5 对齐。

### 第八条：104 个文件的分账是一笔封闭账

```powershell
# 与附录 B 的 B.5 第四条是同一段脚本，这里只记死口径：
#   38 个主机对象   32,746 行
#   24 个卡侧文件   20,232 行
#    5 个死代码文件  2,797 行（trace_capture/）
#   36 个共享头      9,775 行（include/，两边都编）
#    1 个 host/vhost/vhost.h  261 行
# 文件数 38 + 24 + 5 + 36 + 1 = 104（整树的 124 个文件里，除这 104 个 .c/.h 外还有 20 个构建与元数据文件）
# 行数 32,746 + 20,232 + 2,797 + 9,775 + 261 = 65,811
```

　　这笔账是封闭的：五个分项的文件数与行数各自相加，正好等于整棵树的 104 个 `.c`／`.h`、65,811 行（树里另外 20 个构建与元数据文件、934 行，本就不在编译或移植范围内）。报告里凡说「卡侧多少行」，用的都是**主机上恒不编译的 29 个文件、23,029 行**（24 个卡侧文件 20,232 行加 5 个死代码文件 2,797 行）这个口径；36 个头文件另计，它们随两边编译。

### 第九条：第五章 §5.2 的八维统计

```powershell
$env:PYTHONIOENCODING='utf-8'
python _work\check05.py
```

　　它逐维打印 `dimN files … lines … card … dead …`，随后三行分别是八维合计的 73 个文件、1103 行，以及 §5.5 那段页大小口径的 29 个文件、265 行。任何一格与第五章 §5.2 的表不符都会打印 `FAIL`，末行是 `fails 0`。那张表的每一格都对应这里的一次计数。

### 第十条：全报告的源码引用都能在盘上落到原文

```powershell
$env:PYTHONIOENCODING='utf-8'
python _work\check_cites2.py
```

　　它把 `report/` 里全部形如 `路径:行号` 的引用抽出来分三类就地解析：主机路径落在 `_work/mpss-modules-3.8.6/`，上游内核路径落在 `_work/.kcache/` 与 `_work/raw/`，用户态路径落在 `_work/` 下的各个 3.8.6 源码包目录。任何一条找不到落点、或行号超出该文件的行数，都会打印 `LOCAL-EOF`／`KERN-EOF`／`USER-EOF`／`UNRESOLVED`。当前结果是 329 条主机路径、41 条上游、20 条用户态引用全部命中，末行 `fails 0`。

---

## C.6　报告里明确标注「未能核实」的事项索引

　　「未能核实」在本报告里表示**报告不打算猜**。它们集中在第七章末尾的附录里，其中影响最大的几项是：

| 事项 | 为什么核实不了 | 在哪一章 |
|---|---|---|
| 真实的 BAR0/BAR4 尺寸 | 需要真实板子读 PCI 配置空间 | 七章附录、十一章 |
| 固件是否提供 8 GiB 位于 4 GiB 以上的可预取 MMIO 窗口 | 同上 | 七章 7.4、十一章第 1 步 |
| 固件写下的 `_DMA` 位宽 | 需要读 ACPI DSDT 或在机器上打印 `*dev->dma_mask` | 七章 7.6、十一章第 3 步 |
| 设备是否被平台声明为 IO 一致 | 需要上机量 | 七章 7.6、十一章第 4 步 |
| 龙芯上 `ioremap_wc()` 实际落到哪种缓存属性 | 需要读该版本的 LoongArch `pgtable.h`，或上机读页表 | 七章 7.7 |
| 卡的 SCIF 在找不到主机对端时是否优雅退让 | 需要卡上 Linux 的实测 | 十章 10.7、十一章第 2 步 |

　　其余「未能核实」项见第七章附录的完整清单。这份索引把「不能确定的」和「已确定的」分开摆。
## C.7　真机编译实测的证据文件

　　第八章 §8.3 丁 那一节写的是 2026 年 10 月在龙芯机器（AOSC OS 13.3.1，内核 `7.1.13-aosc-main-16k`，16 KB 页）上把这份源码编成 `mic.ko` 的实测。它的证据不在报告里，而在下面这些文件里。

| 文件 | 是什么 |
|---|---|
| `_work/remote-logs/build1.log` … `build9.log` | 九轮构建的原始日志。`build4.log` 是未改动全量那一轮（564 条 error、37 个单元失败），`build9.log` 是成功那一轮（0 error，`mic.ko`） |
| `_work/remote/*.sh` | 远端探针与构建脚本：`probe.sh`（环境）、`api-probe.sh`／`api-probe2.sh`／`probe_ffs.sh`／`probe_werror.sh`（内核 API 现状）、`build_tree.sh`（通用构建）、`verify_ko.sh`（产物验收） |
| `_work/port_patches/batch1.py` 等 | 移植补丁脚本，每条规则都要求命中次数精确，数目不符就不写盘 |
| `_work/port-7.1.13/` | 移植后的工作副本（干净树 `_work/mpss-modules-3.8.6/` 未改动，报告全篇行号仍指它） |
| `_work/port_patches/diff_stat.py` | 「真实改动行数」的算法：逐文件 unified diff，输出 +318 / -309 行、185 个改动块、28 个文件 |
| `_work/remote-build-status.md` | 这一轮实测的汇总记录（环境、五次构建、改动清单、漂移对照） |

　　闸门一与后续三步（设备认出、卡被点亮、数据面）的实测记录另有一批文件，都在 `_work/` 与服务器上的 项目根目录：

| 文件 | 是什么 |
|---|---|
| `g1_load.log` | 加载 `mic.ko` 的门禁检查与结果（含未签名模块 taint、DMA 掩码变化、`/proc/iomem` 占用） |
| `g1_boot.log` | 灌 k1om 内核 + initramfs 的全过程：`state` 从 ready→booting→online、26 秒、卡侧 cmdline |
| `g3_ping.log` | 第一次数据面试验（100% 丢包）与原因定位 |
| `g3_remaster.log` | 给卡端镜像补 `auto mic0` 之后重灌并 ping 通的全过程 |
| `card_console.log` | 从主机 `/dev/ttyMIC0` 抓到的卡控制台输出（卡端 Poky 3.8.6 的登录横幅） |
| `_work/remote/g*.sh` | 上面每一步用的脚本（都带时间戳日志，可重跑） |

　　第三批（SSH 与吞吐）的文件：

| 文件 | 是什么 |
|---|---|
| `g4_ssh.log` | 按 micctrl 的密钥模型注入 `authorized_keys`、重灌卡、等 sshd、`ssh mic0` 测试、512 MB 双向 `md5sum` 比对与吞吐的全过程 |
| `g5_through.log`、`g5_final.log`、`g5_final2.log`、`g5_confirm.log` | 明文与加密吞吐的多轮测量（含测量方法本身的几次返工：`nc -l` 缺 `-N`、卡侧 BusyBox `nc` 不支持监听、后台任务 stdin 被赋为 `/dev/null`） |
| `card_console.log` 之外的 `g3_remaster*.log` | 卡端镜像三次改动（网口、SSH、host key）的完整过程记录 |

　　第四批（工具端移植）的文件与目录：

| 位置 | 是什么 |
|---|---|
| `mpss-userland/tar/` | 从交付里取出的八个源码包（libscif、mpss-metadata、gen-symver-map、mpss-daemon、mpss-micmgmt、miccheck、mpss-coi、mpss-myo） |
| `mpss-userland/src/` | 解包并按附录 E 改过的源码树 |
| `mpss-userland/stage/` | 自己的安装前缀（`usr/lib64`、`usr/include`、`usr/include/mic`），供各组件互相编译 |
| `mpss-userland/logs/` | 每个组件的构建日志与 root 验收日志 |
| `patch*.py`、`gen_defsym.py` | 附录 E 里每一条改动的补丁脚本（幂等，可重跑） |
| `_work/remote/g6_*.sh`、`g7_*.sh`、`g8_*.sh` | 每个步骤的驱动脚本（带时间戳日志） |

