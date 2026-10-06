# CHANGELOG — MPSS 3.8.6 LoongArch 移植版

　　本文件是**总项目**的总结性变更记录：按日期给出功能更新摘要，每条指向对应子项目的 CHANGELOG —— 细节在那里，本文件不重复。英文版见 [CHANGELOG.md](CHANGELOG.md)。

　　约定：条目以「一项功能更新」为单位；日期即该功能落地的日子；新增内容追加在最尾端，便于持续增编。
## 2026-10-02

- **静态审计完成**　报告主体 12 章与附录 A–D 定稿：判定可移植，指出接口欠账 86 行、真正与 x86 绑死的只有两处、唯一可能否决整件事的是固件是否给出 8 GiB 高位可预取窗口。→ [docs](docs/CHANGELOG_CN.md)

---
## 2026-10-04

- **主机内核模块首次在龙芯上编出并 probe 成功**　`mic.ko` 为 LoongArch ELF、`modinfo` 可读；BAR0 拿到 16 GiB 且位于 4 GiB 以上，两个 DMA 掩码均为 64 位；写入引导命令后 26 秒卡进 `online`，主机与卡 ICMP 往返 0% 丢包。→ [08-mic-module](08-mic-module/CHANGELOG_CN.md)
## 2026-10-05

- **用户态工具端整条编成并在真机跑通管理面**　`libscif`、`libmpssconfig`、`mpssd`、`micctrl`、`libmicmgmt`、`mpssinfo`、`mpssflash`、`micsmc`、`miccheck` 全部在 loongarch64 上落地；`micctrl --status`、`mpssinfo`、`miccheck`、SSH 登录、经 SCIF 在卡上建用户等路径当场跑通。→ [02-libscif](02-libscif/CHANGELOG_CN.md)、[03-mpss-daemon](03-mpss-daemon/CHANGELOG_CN.md)、[04-mpss-micmgmt](04-mpss-micmgmt/CHANGELOG_CN.md)、[05-miccheck](05-miccheck/CHANGELOG_CN.md)
- **系统集成**　主机守护进程以 systemd 单元自启动（`Type=simple` ＋ 前台运行），卡网口 `mic0` 由随包单元与脚本按 MPSS 配置自动配置，设备节点权限用现代 udev 规则放开，使普通用户即可使用。→ [03-mpss-daemon](03-mpss-daemon/CHANGELOG_CN.md)、[08-mic-module](08-mic-module/CHANGELOG_CN.md)
- **k1om 工具链在龙芯上可用（配套）**　让 MPSS 自带的 k1om 编译器在龙芯上运行并固化成包装脚本；自建卡端 `libgomp`（MPSS 从未提供）；卡端 offload worker 构建并上卡。卡上第一次真算：2 亿项归约，61 线程 36.3 倍加速。→ [06-mpss-coi](06-mpss-coi/CHANGELOG_CN.md) 的「配套项」
- **MYO 宿主库**　`libmyo-client.so` 在 loongarch64 上构建并跑通，x86 专用实现（`rdtsc`、`lock; xaddl`、SSE2 差分层）逐项替换为等价写法。→ [07-mpss-myo](07-mpss-myo/CHANGELOG_CN.md)
- **卡端引导镜像可用化**　initramfs 补静态网口配置与授权密钥（原交付里没有，正常由主机侧下发）。→ [09-boot-images](09-boot-images/CHANGELOG_CN.md)
- **符号版本与别名的两条路径**　符号版本映射工具改用 Python 3；新增从共享库生成链接期别名的工具，使非 x86 宿主上公开 ABI 名（`COI_1.0` 一类）保持不变。→ [00-build-tools](00-build-tools/CHANGELOG_CN.md)
- **发布版打包（v0.1）**　九个包各自 `make install`，含暂存安装与改前缀支持；顶层 Makefile 按依赖顺序一次装完。→ [README_CN.md](README_CN.md)
## 2026-10-06

- **页大小与跨端结构体布局修复**　宿主页不是 4 KiB 时的两类缺陷一并修掉：注册长度按 4096 字节的协议页校验并双向换算；`packed` 结构里自旋锁落在非对齐地址的问题用「等待队列指针化 ＋ 等长填充」解决，同时保持与卡端逐字节相同的布局。数据面由卡端逐字节校验确认。→ [08-mic-module](08-mic-module/CHANGELOG_CN.md)
- **offload 端到端打通**　宿主侧 COI 能真的在卡上建进程、按名取函数、跑卡端 OpenMP 并把结果取回来；三处阻塞缺陷（创建命令搬运、对端窗口长度、卡端缺 `-rdynamic`）修复。实测 240 线程、相对误差 −3.35e-16，N 体示例的校验和与宿主参考实现逐位一致。→ [06-mpss-coi](06-mpss-coi/CHANGELOG_CN.md)
- **验收测试套件建立并在真机全绿**　T0–T8 加两个辅助脚本，合计 72 项通过、0 项失败、1 项跳过；其中 T4 覆盖数据面七档长度、T5 覆盖 COI 端到端、T7 覆盖重计算与逐位一致性。→ [tests](tests/CHANGELOG_CN.md)
- **测试套件与环境解耦并随发布树交付**　测试项目成为发布树的一级目录 `tests/`，路径全部改为「环境变量 → `tests/config.sh` → 自动探测」，探测不到即明确跳过并提示该设哪个变量；实测只填一行配置即可跑绿。→ [tests](tests/CHANGELOG_CN.md)
- **offload 编程手册重写**　从「项目纪实」改为**以 COI API 为纲**的手册：七个部分 ＋ 六个附录，逐条解释接口（头文件里 63 个公开函数全覆盖），并给出「按问题找章节」索引；不依赖任何具体机器布局。→ [docs](docs/CHANGELOG_CN.md)
- **报告回写与自我更正**　报告新增附录 J（验收测试套件），附录 H 增补 COI 打通与**一条被否证的判据**（掩码压缩存储指令不能作为崩溃判据），附录 I 的未决项改为已解决并纠正原归因。→ [docs](docs/CHANGELOG_CN.md)
## 子项目一览

| 子项目 | 目录 | CHANGELOG |
|---|---|---|
| 构建辅助工具 | `00-build-tools/` | [CHANGELOG_CN.md](00-build-tools/CHANGELOG_CN.md) |
| 用户态 SCIF 库 | `02-libscif/` | [CHANGELOG_CN.md](02-libscif/CHANGELOG_CN.md) |
| 主机守护进程与管理 CLI | `03-mpss-daemon/` | [CHANGELOG_CN.md](03-mpss-daemon/CHANGELOG_CN.md) |
| 卡管理库与工具 | `04-mpss-micmgmt/` | [CHANGELOG_CN.md](04-mpss-micmgmt/CHANGELOG_CN.md) |
| 自检工具 | `05-miccheck/` | [CHANGELOG_CN.md](05-miccheck/CHANGELOG_CN.md) |
| offload 运行时（COI） | `06-mpss-coi/` | [CHANGELOG_CN.md](06-mpss-coi/CHANGELOG_CN.md) |
| MYO 运行时 | `07-mpss-myo/` | [CHANGELOG_CN.md](07-mpss-myo/CHANGELOG_CN.md) |
| 主机内核模块 | `08-mic-module/` | [CHANGELOG_CN.md](08-mic-module/CHANGELOG_CN.md) |
| 卡端引导镜像 | `09-boot-images/` | [CHANGELOG_CN.md](09-boot-images/CHANGELOG_CN.md) |
| 文档与离线站点 | `docs/` | [CHANGELOG_CN.md](docs/CHANGELOG_CN.md) |
| 验收测试套件 | `tests/` | [CHANGELOG_CN.md](tests/CHANGELOG_CN.md) |

---
## 2026-10-06 — 文档成对交付（中英双版）与目录落定

- **新增**　全部文档补齐英文版：报告 12 章 ＋ 附录 A–K ＋ 文档导读 ＋《KNC offload 编程手册》，共 26 份英文文档；命名约定为中英成对（中文 `*_CN.md`，英文为同名去掉 `_CN`），正文引用各自指向同一语言。→ [docs](docs/CHANGELOG_CN.md)
- **变更**　文档目录落定：发布树根下 `docs/`（报告与编程手册）与 `tests/`（验收测试套件）与九个包并列，两个子项目各自带中英双版的 README 与 CHANGELOG。→ [docs](docs/CHANGELOG_CN.md)、[tests](tests/CHANGELOG_CN.md)

## 2026-10-06 — T8（大数据量传输）与页大小第二批修复

- **新增**　验收测试新增 **T8**：默认把 4 GiB 送到卡上，卡端大块 `malloc`、逐窗口校验、两端整缓冲 checksum 比对，并测算带宽。真机三档全绿，4 GiB 两端**逐位一致**，链路侧 325.9 MB/s、端到端 98.8 MB/s。→ [tests](tests/CHANGELOG_CN.md)
- **修复**　页大小家族的第二批驱动缺陷（宿主自身窗口页数被换算两次；`micscif_get_dma_addr` 两分支页大小不一致 + 对端页数被整除成 0）。修好之前，T4 之后 10 秒必现 `mic_smpt.ref_count < 0` 并最终把 RMA 通路彻底卡死（只能重启）。→ [08-mic-module](08-mic-module/CHANGELOG_CN.md)
- **变更**　报告增补：附录 I 的 I.12（缺陷链条与判据）、附录 J 的 T8 实测与五条固化坑。→ [docs](docs/CHANGELOG_CN.md)

## 约定

- 本文件只记功能更新，不记重构与文案调整；一条更新的判据与证据放在对应子项目的 CHANGELOG 里。
- 子项目各自维护自己的 CHANGELOG，本文件只在条目里给出一句摘要与链接。
- 需要新增条目时，直接在最上面加一个新日期节（同一天可有多节），并在子项目 CHANGELOG 里补细节。

## 2026-10-07 — 对端异常描述的防线、诊断打印门控、RMA 上限标定与三篇 API 规范文档

- **修复**　宿主侧新增「窗口描述完整性校验」，并把 `micscif_get_dma_addr()` 的 `BUG_ON(1)` 改为返回 `RMA_ERROR_CODE`：同一个坏输入（32 MiB 卡端窗口）由「内核 BUG + 进程 `D` 状态 + 必须重启」变为「一次带原因的失败」。细节见 [08-mic-module/CHANGELOG_CN.md](08-mic-module/CHANGELOG_CN.md)。
- **变更**　诊断打印全部纳入 `MIC_DEBUG` 开关（默认静默），失败原因保留常开。同样参见 [08-mic-module/CHANGELOG_CN.md](08-mic-module/CHANGELOG_CN.md)。
- **标定**　单次 `scif_writeto` 由保守的 26496 字节放宽为**已验证到 1 MiB**（26496 B → 64/128/256/512 KiB → 1 MiB，各档两端 checksum 一致）。细节见 [tests/CHANGELOG_CN.md](tests/CHANGELOG_CN.md)。
- **新增**　文档三篇：附录 K（内存搬运用户侧约束）、附录 L（逐 API 确定性规范）、附录 M（可移植性与相容性）；附录 F 增补 **F.9 约束清单**并指回 K/L；`docs/` 44 对中英齐全。细节见 [docs/CHANGELOG_CN.md](docs/CHANGELOG_CN.md)。
- **新增**　测试：`tests/extra/` 细粒度用例集（与 `run_tests.sh` 分开，含通用探针与 x01–x04）。细节见 [tests/CHANGELOG_CN.md](tests/CHANGELOG_CN.md)。
- **更正**　附录 I 的 I.12.6 原先"12 位页数上限"的论证**被源码与实测否证**，已改写为定案：卡端段表只写第一页（512 项）；卡端内核模块重建**列为可选路径**（附录 K 的 K.6，含配方与代价）。细节见 [docs/CHANGELOG_CN.md](docs/CHANGELOG_CN.md)。
- **核对**　`patches/` 全部文件与龙机在编译源码 md5 一致；本地↔龙机发布树逐字节一致。

