# CHANGELOG — 主机内核模块（08-mic-module）

　　本文件记本包**功能更新**：内核模块 `mic.ko` 的移植、随包安装的 modprobe／udev 配置与卡网口自动配置，以及页大小相关的驱动补丁。以「一项功能更新」为单位成条。新增内容追加在最尾端。

---
## 2026-10-02 — 静态审计：接口欠账与架构耦合

- **新增**　逐族核对内核接口漂移，确认需要改动的共 **86 行**：64 行纯机械替换、22 行需按语义处理；另标明 `host/vhost/` 与 `host/linvnet.c`／`vnet/` 两块只核到族级。
- **新增**　判定与主机指令集真正绑死的只有两处：F1（用 Intel CPU 型号查表，语义死结）与 F6（`micscif_ports.c` 的三行 x86-64 内联汇编）。
- **新增**　确认「主机只做三件事」：把内核镜像写进卡存窗口、敲门铃寄存器、收一个中断；卡侧代码一行不用移植。

---
## 2026-10-04 — 首次在龙芯机器上编出 `mic.ko` 并 probe 成功

- **新增**　内核模块编译通过：`make -k -j8 MIC_CARD_ARCH=k1om KERNWARNFLAGS=-Wno-error`，得到 23,254,080 字节的 LoongArch ELF，`modinfo` 可读（`vermagic: 7.1.13-aosc-main-16k SMP preempt mod_unload LOONGARCH 64BIT`）。
- **新增**　真机 probe 成功：BAR0 拿到 **16 GiB** 且位于 4 GiB 以上，两个 DMA 掩码均为 64 位；写入引导命令后 **26 秒**卡进 `online`；主机与卡 ICMP 往返 0% 丢包（含 60 KB 大包）。
- **修复**　一批内核接口漂移（batch 1，19 条／7 个文件）：`MAX_ORDER` → `MAX_PAGE_ORDER`、`mm->mmap_sem` → `mmap_lock`、`pinned_vm` 改 `atomic64`、`pci_map_*`／`PCI_DMA_*` → `dma_*` 与 `DMA_*`、`PDE_DATA` → `pde_data`、proc 文件改 `proc_ops`、`timespec` → `timespec64`、`bin_attribute` 回调加 `const`，以及 F7（`slow_virt_to_phys` 分支删除，只留 `vmalloc_to_page`）。
- **变更**　`host/vmcore.c` 摘出构建：该文件在整棵树里没有 `struct vmcore` 的定义（源码自身不完整），另与内核同名符号原型冲突；改在 `host/uos_download.c` 提供返回 `-EOPNOTSUPP` 的 `vmcore_create()` 桩，唯一调用点本来就按返回码分支。
- **验证**　全量构建错误从 564 条收敛到 0；真实改动 28 个文件、+318 / −309 行、185 个改动块（静态审计阶段估计 86 行，差额记在 `docs/08-migration-roadmap_CN.md`）。
## 2026-10-05 — 随包安装物与设备权限

- **新增**　`mic.modules-load`：把 `mic` 写入 `/etc/modules-load.d/`，由 systemd 在启动早期加载模块；驱动同时声明了 `MODULE_DEVICE_TABLE(pci, …)`，udev 在设备出现时也会自动 `modprobe`。
- **新增**　udev 规则把 `/dev/mic/scif`、`/dev/mic/ctrl` 放开为 0666。原规则的 `NAME=` 旧写法在 systemd-udev 下被忽略，设备会退回 root 独占，于是「普通用户用不了」被误当成移植缺陷。
- **新增**　卡网口自动配置三件套：`mic0-net.service`、`/usr/libexec/mpss/mic0-up.sh`、`90-mic0-net.rules`。网口 `mic0` 由本模块创建（virtio-net 设备一出现即建），地址与 MTU 则由这三件套按 `/etc/mpss/mic0.conf` 的 `Network` 行配置。
- **验证**　普通用户可打开 SCIF 设备；`mic0` 出现后自动 up 并带预期地址；`tests/t1_install.sh` 覆盖安装与权限判据。
## 2026-10-06 — 页大小与两端结构体布局修复

- **修复**　16 KiB 页宿主上 `scif_register` 被静默拒绝（用户态按 4096 字节注册，而驱动按宿主页校验对齐）。改为按**协议页**（4096 字节）校验长度，内部仍按宿主页向上取整来 pin 内存；宿主页等于 4096 时行为与上游完全一致。
- **修复**　`struct reg_range_t` 的 `packed` 属性把内嵌等待队列里的自旋锁放到了非 4 字节对齐地址，LoongArch 内核态遇到非对齐原子访问会直接终止进程（表现为 `ALE`，出错地址＝基址＋150，且 150 是编译期成员偏移）。四个等待队列（`allocwq`、`regwq`、`unregwq`、`gttmapwq`）改为「指针 ＋ 等长填充」：既让锁落在对齐地址，又保持与卡端逐字节相同的结构体布局。
- **修复**　RMA 拷贝路径按页步长计算错误，导致 26496 字节的批量写只有第一页正确（正是 COI 创建命令的尺寸）。
- **修复**　跨端结构体一律恢复并保持 `packed`：宿主单方面去掉该属性会造成两端字段偏移不一致，症状是「读回的字段是垃圾」（`ADEM`，坏地址落在物理／非缓存窗口）。
- **变更**　跨到卡端的页数量在送出前按 $P_h/P_c$ 换算成对端页；宿主内部一律维持「宿主页」语义，不做全局 `PAGE_SHIFT` 替换，以免破坏偏移对齐与 mmap 语义。
- **验证**　卡端逐字节校验 4096 字节 0 不符；内核日志出现 `nr_pages=4`（宿主 1 个 16 KiB 页＝对端 4 个 4 KiB 页）；`unaligned` 计数归零；描述区 `magic` 等于 `SCIFEP_MAGIC`；验收脚本 `tests/t4_rma_dma.sh`（15 项）覆盖。
- **附**　本补丁的改动文件放在 `patches/`：`micscif_rma.c`、`micscif_rma.h`、`micscif_rma_dma.c`、`micscif_rma_list.c`、`micscif_nodeqp.c`，随下次发布并入模块源码树（放置位置见 `patches/README_CN.md`）。
## 2026-10-06 — 设备权限：现代 udev 规则随包安装（更正）

- **新增**　`55-mic-perms.rules` 随包安装到 `/usr/lib/udev/rules.d/`，把 `/dev/mic/scif`、`/dev/mic/ctrl` 放开为 0666；`make install` 后立刻 `udevadm trigger` 并 `chmod` 现有节点，不必重载模块。
- **更正**　此前"已在机器上放开设备权限"只对测试环境成立：`t1_install.sh` 会写一份现代规则到 `/etc/udev/rules.d/50-udev-mic.rules`，而**普通 `make install` 装的是上游那条旧式规则**（`NAME="mic/%k"`，systemd-udev 忽略该写法，同一条里的 `MODE="0666"` 也就落不到节点上）。于是重装或重启后设备退回 `crw------- root:root`，普通用户打不开，看起来像"驱动坏了"。
- **验证**　`ls -l /dev/mic/scif` 模式为 `crw-rw-rw-`；测试 T1 以普通用户真开一次设备作为判据。

## 2026-10-06 — 页大小家族的第二批修复：页数被换算两次、段跨度页大小不一致

- **修复**　**宿主自身窗口的页数被换算两次**。`micscif_map_window_pages()` 把**所有**窗口的页数按对端单位（×4）打包进 `dma_addr[]`，而读回端 `micscif_set_nr_pages()` 只对 `RMA_WINDOW_PEER` 做反换算，自有窗口原样使用 —— 于是 `num_pages[]` 变成真实值的 4 倍。后果链：解映射多拆 4 倍页 → `mic_smpt[i].ref_count < 0`（WARNING）→ 11 秒后 `micscif_get_dma_addr` 找不到地址 `BUG()` → 窗口注销永远等不到对端回应（`wait_event_timeout` 超时后 `goto retry`，只要对端活着就无限重试，dmesg 里是每 15 秒一条小 DMA）→ 进程 `D` 状态、`kill -9` 无效、`rmmod` 报"模块正被使用"，只能重启。改法：本地一律按宿主页记账，线上换算只作用于**发给对端的那份副本**。
- **修复**　**`micscif_get_dma_addr()` 两个分支对页大小的理解不一致**。一段一页时按窗口所属一侧取 4 KiB／16 KiB（本来就对），多页段时却一律用宿主 `PAGE_SHIFT`；叠加 `micscif_set_nr_pages()` 把对端页数 ÷4，卡端常见的「一段一页 = 4 KiB」被整除成 **0** → 段跨度算成 0 字节 → 区间判定永不成立 → 扫完所有段后 `BUG_ON(1)`。这正是 T4 从不触发的原因：它的窗口都是一段一页，走的是正确的那条分支。
- **新增**　`BUG` 之前打印窗口几何（`type`、`offset`、`nr_pages`、`nr_contig_chunks`）与前 8 段的 `num_pages`／跨度／地址，以及所用页单位，单位错误一眼可见。
- **实测**　T4 之后等 30 秒 dmesg 事故 **0** 条（修复前必现）；T8 三档 64 MiB／256 MiB／4 GiB 全量传输、两端 checksum **逐位一致**（4 GiB = `0x76ac888ab487e57b`）；4 GiB 链路侧 **325.9 MB/s**、端到端 **98.8 MB/s**；无内核异常、无卡死进程。完整链条见 `docs/I-页大小对齐调查.md` I.12。

## 2026-10-06 — 诊断打印改为 MAKE 时可控的编译选项；仓库行尾归一为 LF

- **变更**　SCIF 诊断打印（14 处 `pr_info`）统一改为 `mic_dbg()`（新增 `include/mic/mic_debug.h`）：默认编译为 `pr_debug` —— 不打开 dynamic debug 即完全静默；`make MIC_DEBUG=1` 时编译为 `pr_info`，直接进 dmesg。原因：全部打开时**一次 4 GiB 传输要写两万多行 dmesg**（实测本次开机累计 23694 行全是这些探针），发布版不应如此；而排查时它们又确实有用，所以做成开关而不是删掉。
- **新增**　`Kbuild` 增加该开关。**必须用 `ifneq ($(MIC_DEBUG),)` 判断**：写成 `subdir-ccflags-$(MIC_DEBUG)` 会展开成 `subdir-ccflags-1` 而被 Kbuild 忽略——实测加了开关但两次构建产物字节完全相同（23405184 字节，md5 一致）；改用 `ifneq` 后两份产物分别为 23405184／23367704 字节，md5 不同，开关生效。包装 `Makefile` 透传 `MIC_DEBUG` 并把用法写进注释。
- **修复**　**仓库行尾**：`micscif_nodeqp.c`、`include/mic/micscif_rma.h`、`dma/mic_dma_lib.c` 三个文件此前以 CRLF 入库，导致它们与工作树的 diff 变成整文件重写（2902／960／1792 行），语义改动被淹没。已拆成两个提交：`d8125be`「仅行尾归一为 LF（无逻辑改动）」与 `11a16e8`「语义改动」；`.gitattributes` 增加 `* text=auto eol=lf` 防复发。归一后 `mic_dma_lib.c` 的真实改动只剩 **16 行**（原显示 1807 行）、`micscif_rma.h` **85 行**（原 1032 行）、`micscif_nodeqp.c` 17 行（原 2911 行）。
- **补全**　`patches/` 由 5 个文件扩为**改动全集 10 个文件**：新增 `micscif_api.c`、`micscif_rma_dma.c`、`micscif_rma_list.c`、`micscif_nodeqp.h`、`mic_dma_lib.c`、`Kbuild`、`mic_debug.h`；README 逐行给出目标位置与构建开关、验收判据。
- **核对**　补丁目录里的每个文件与龙机在编译的源码 **md5 全等**；本地与龙机发布树 132 个共有文件逐字节一致。

## 2026-10-06 — `scif_register` 增加 12 位页数上限守卫（把必然卡死变成干净失败）

- **修复**　线上窗口描述里「每个连续段的页数」只有 **12 位**（`RMA_SET_NR_PAGES` 里的 `& 0xFFF`），单段最多 4095 页。超限时 `RMA_SET_NR_PAGES` **静默截断**，读回端 `micscif_set_nr_pages()` 拿到 0 后提前 `break`，段跨度算成 0 字节，`micscif_get_dma_addr()` 扫完全部段仍找不到地址而 `BUG_ON(1)`，随后就是「注销无限重试 → 进程 `D` 状态 → 只能重启」。实测触发：卡端一次注册 1 GiB（262144 个 4 KiB 页）走直连模式，传到 74% 崩溃（现场见 `docs/I-页大小对齐调查_CN.md` I.12.6）。
- **新增**　`__scif_register()` 在 `scif_pin_pages()` 成功之后、`micscif_prep_remote_window()` 之前逐段检查：任一段超过 4095 页即回收窗口与 pinned pages、返回 `-EINVAL`，并在 dmesg 写明是哪一段、多少页、上限多少。上限：宿主 16 KiB 页时单段约 63 MiB，卡端 4 KiB 页时约 15 MiB；判据是**单段**页数而非注册总长。
- **作用**　把「必然卡死、只能重启」变成「一次干净的注册失败」；卡端用同一份源码，因此两侧同时生效（卡端的注册探测循环会在失败后逐级减半，自动退到可用跨度）。
- **实测**　守卫插入后 `make` 通过（`mic.ko` 23416776 字节）；失败路径复用 pin 失败的清理序列（`micscif_destroy_incomplete_window` → `dec_node_refcnt` → `scif_unpin_pages` → `__scif_release_mm`）。
- **踩坑记录**　守卫第一版把 `RMA_HUGE_NR_PAGE_MASK`（移位后的掩码 `0xFFF<<52`）当成页数上限并强转 `int`：低 32 位为 0，于是 `1 > 0` 恒真，**每次注册都被拒**（T4 8/15、T8 全档失败、客户端 `scif_register: Invalid argument`），而内核零异常。已改为 `RMA_MAX_NR_PAGES_PER_CHUNK`（右移得到的 4095，见 `include/mic/micscif_rma.h`）并用独立小程序验证边界（1 页不超限／4096 页超限）。守卫打印同时改为 ASCII —— 中文进 dmesg 会被转义，`grep` 中文判据会得到假的"0 次拒绝"。

## 2026-10-06 — 对端描述不完整时不再 panic：`BUG_ON` 改为 `RMA_ERROR_CODE`

- **修复**　`micscif_get_dma_addr()` 在找不到地址时曾 `BUG_ON(1)`（宿主构建下生效），把 `RMA_ERROR_CODE` 这条**早已设计好、且在 `micscif_rma_dma.c` 里被 10 处判读**的错误路径变成了死代码。panic 的代价是：窗口注销再也等不到对端回应 → 进程 `D` 状态 → 只能重启。现已改为打印完整现场后 `return RMA_ERROR_CODE`，错误由调用方上抛，用户态 `scif_writeto` 正常失败。
- **背景（实测）**　卡端跑的是**未打补丁的上游模块**（`card-modules/micscif.ko` 中移植探针 0 处、源码为上游原始时间戳），没有 12 位页数守卫。32 MiB 卡端窗口会送来末尾 15 段页数为 0 的描述：宿主探针实测 `nr_pages=8192 nr_contig_chunks=527 loop_stopped_at=512`，跨度总和 33492992 字节 vs 应有 33554432，差 **61440 = 15 × 4096**，与 15 个零值段完全自洽。
- **新增探针（常开）**　`MIC scif DESC-INCONSISTENT`（读回端：段数、总页数、首个零值段、前 4 个打包值）与 BUG 现场的「各段跨度总和 vs 窗口应有字节数、零值段个数」——用来区分"对端描述短了"与"查找逻辑错了"，本轮正是靠它定的案。
- **影响**　"卡端零改动"的结论依然成立：宿主能检测并拒绝不完整的描述，而不是被它拖死。

## 2026-10-07 — 调试打印纳入 `MIC_DEBUG` 开关（默认静默；失败原因保留常开）

- **变更**　`include/mic/micscif_rma.h` 中最后 7 处"因调试而常开"的打印改为 `mic_dbg()`：`RAW-SCAN`／`RAW-HEAD[n]`／`RAW-TAIL[n]`（剥离页数前的原始值转储）、`DESC-INCONSISTENT`、以及失败路径上的 `chunk %d: num_pages=…`／`(page unit used=…)`／`chunk spans total=…`。
- **保留常开**（属于"失败原因"而非调试噪音）：`micscif_window_desc_valid()` 的 `DESC-BAD … refusing the copy`、`scif_register` 的 12 位守卫拒绝、上游原有的 `Addr not found` 标题与全部上游 `KERN_ERR`。
- **实测**　T4 **15/0**（30 秒后事故 0）；用同一个坏输入（32 MiB 窗口）复现：`RAW-SCAN` 新增 **0**、`DESC-INCONSISTENT` 新增 **0**（已门控 ✓），`DESC-BAD` 新增 **1**（原因仍可见 ✓），`kernel BUG` 新增 **0**、无 `D` 状态、卡 `online` ✓；随后 T8 64 MiB 仍 **12/12**、两端 checksum 一致 ✓。dmesg 现场由多行转储精简为一行：
  `MIC scif DESC-BAD (dst): type=2 nr_pages=8192 chunks=527 sum_of_chunks=8177 zero_count_chunks=15 -> refusing the copy`
- **用法**　需要这些转储时 `make MIC_DEBUG=1` 重新构建安装，或用 dynamic debug 单独打开；两种构建下字符串都保留在模块内（已实测模块中存在 `RAW-SCAN`/`RAW-TAIL`/`DESC-INCONSISTENT`/`DESC-BAD`）。

## 相关文档

- 页大小与结构体布局的完整调查：`docs/I-page-size-alignment_CN.md`
- 接口漂移逐族清单：`docs/06-kernel-api-drift_CN.md`
- 移植路线与真机实测：`docs/08-migration-roadmap_CN.md`
- 验收测试（T0 编译、T1 安装、T4 数据面）：`tests/`
