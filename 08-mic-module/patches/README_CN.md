# patches/ — 下一次发布要并入内核模块源码树的改动文件

　　本目录是**本次移植对上游 `mpss-modules-3.8.6` 主机侧模块源码的改动全集**（10 个文件）。照下表放好、照常 `make`，就得到移植后的模块；**卡端一行都不用改**——补丁的全部效果都在宿主侧。

## 放哪里

| 本目录文件 | 目标位置（相对上游模块树根） | 改了什么 |
|---|---|---|
| `Kbuild` | `Kbuild` | 诊断打印开关：`MIC_DEBUG=1` 时加 `-DMIC_SCIF_DEBUG_PRINT`（用 `ifneq` 判断，见下） |
| `mic_debug.h` | `include/mic/mic_debug.h` | **新增文件**：`mic_dbg()` 宏 —— 默认 `pr_debug`，打开时 `pr_info` |
| `micscif_rma.c` | `micscif/micscif_rma.c` | 协议页校验与宿主页 pin、等待队列指针化、**页数按宿主页记账**、RMA 步长、诊断打印 |
| `micscif_rma.h` | `include/mic/micscif_rma.h` | `struct reg_range_t` 指针化 ＋ 等长填充、协议页宏、**两个分支按侧取页大小**、`BUG` 前打印窗口几何 |
| `micscif_rma_list.c` | `micscif/micscif_rma_list.c` | 窗口注册／注销路径配套（含诊断） |
| `micscif_rma_dma.c` | `micscif/micscif_rma_dma.c` | RMA 拷贝路径按协议页／宿主页换算（含诊断） |
| `micscif_nodeqp.c` | `micscif/micscif_nodeqp.c` | 节点消息路径（等待队列指针化配套与诊断） |
| `micscif_nodeqp.h` | `include/mic/micscif_nodeqp.h` | 同上 |
| `micscif_api.c` | `micscif/micscif_api.c` | `scif_register`／pin 的协议页校验与拒绝日志 |
| `mic_dma_lib.c` | `dma/mic_dma_lib.c` | DMA 引擎层接口迁移（`dma_mapping_error`／`pde_data`／`proc_ops`）与诊断 |

## 构建开关：诊断打印

　　`mic_dbg()` 默认编译成 `pr_debug` —— **不打开 dynamic debug 就完全静默**，不污染 dmesg；需要时：

```bash
make MIC_DEBUG=1            # 构建时打开（编译成 pr_info，直接进 dmesg）
make install MIC_DEBUG=1    # 构建并安装
```

　　为什么要有这个开关：这些探针在移植期用来定位页大小与窗口簿记问题，但全部打开时**一次 4 GiB 传输会写两万多行 dmesg**（实测本次开机累计 23694 行全是这些探针），发布版不应如此。Kbuild 里必须用 `ifneq ($(MIC_DEBUG),)` 判断 —— 写成 `subdir-ccflags-$(MIC_DEBUG)` 会展开成 `subdir-ccflags-1` 而被 Kbuild 忽略（这个坑实测踩过：加了开关但两份产物字节完全相同）。

## 怎么验证

| 判据 | 命令 | 期望 |
|---|---|---|
| 注册与数据面 | `tests/t4_rma_dma.sh` | 15/0：七档长度逐字节正确、26496 全量 0 不符 |
| **页大小修复的关键判据** | T4 之后**等 30 秒**再查 dmesg | `ref_count < 0`、`kernel BUG`、`Oops` **均为 0 条**（修复前必现，且事故在 workqueue 里晚几秒才炸，T4 自己的检查看不到） |
| 大数据量与完整性 | `tests/t8_bigxfer.sh` | 64 MiB／256 MiB／4 GiB 全通，两端 checksum 逐位一致，链路侧 ≈330 MB/s |
| 打印开关 | 默认构建后跑一次传输 | dmesg 里没有 `MIC scif ...` 探针 |
| **12 位页数上限** | 尝试注册一段超过 4095 页的连续内存（宿主 16 KiB 页时 > 63 MiB） | `scif_register` 返回 `-EINVAL` 并在 dmesg 写明"第 N 个连续段含 X 页超过上限"，**不再是** `micscif_get_dma_addr` 的 `BUG` 与随后的卡死 |

　　细节：`docs/I-页大小对齐调查_CN.md` 的 I.11（第一轮：协议页、结构体布局、RMA 步长）与 I.12（第二轮：页数被换算两次、段跨度页大小不一致）；现场数字见 `docs/H-offload实操记录_CN.md` H.13。

## 变更日期

- 2026-10-05：第一轮页大小修复（协议页校验、等待队列指针化、RMA 步长）。
- 2026-10-06：第二轮修复（页数记账单位、段跨度页大小）；补丁集补全为改动全集；诊断打印改为 `MIC_DEBUG` 构建开关。
